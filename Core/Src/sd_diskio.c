/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @brief   FatFs R0.15 low-level disk I/O for SDMMC1 (IDMA, read-only).
  *
  *  - Physical drive 0 = TF card on SDMMC1.
  *  - Reads use HAL_SD_ReadBlocks_DMA (SDMMC internal DMA, interrupt completion).
  *  - SDMMC1 IDMA can reach AXI SRAM (0x2400_0000) and FMC SDRAM (0xD000_0000),
  *    NOT DTCM (0x2000_0000). Unaligned or DTCM buffers go through a bounce
  *    buffer one sector at a time.
  *  - D-Cache is enabled. Buffers in cacheable memory (AXI SRAM, SDRAM source
  *    window 0xD060_0000..+512 KB) must be 32-byte aligned with a length that
  *    is a multiple of 32; they are invalidated before and after each transfer.
  *    Non-conforming buffers go through the aligned bounce buffer.
  ******************************************************************************
  */
#include "main.h"
#include "ff.h"
#include "diskio.h"
#include "sd_diskio.h"

#define SD_TIMEOUT_MS   2000U
#define SD_SECTOR_SIZE  512U

extern SD_HandleTypeDef hsd1;

static volatile DSTATUS s_stat = STA_NOINIT;
static volatile uint8_t s_rx_done;
static volatile uint8_t s_rx_err;

/* 32-byte aligned bounce buffer, placed in AXI SRAM with the rest of .bss */
static uint8_t s_bounce[SD_SECTOR_SIZE] __attribute__((aligned(32)));

/* ---- async job state (video path) ---- */
static sd_seg_t          s_job[SD_JOB_MAX_SEGS];
static volatile uint32_t s_job_n;
static volatile uint32_t s_job_i;
static volatile uint8_t  s_job_active;
static volatile uint8_t  s_job_err;

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (s_job_active)
  {
    if (s_job_err) { s_job_active = 0U; return; }
    s_job_i++;
    if (s_job_i < s_job_n)                    /* chain next contiguous segment */
    {
      const sd_seg_t *g = &s_job[s_job_i];
      if (HAL_SD_ReadBlocks_DMA(hsd, g->buf, g->lba, g->count) != HAL_OK)
      {
        s_job_err = 1U;
        s_job_active = 0U;
      }
    }
    else
    {
      s_job_active = 0U;                      /* whole frame done */
    }
    return;
  }
  s_rx_done = 1U;
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
  if (s_job_active)
  {
    s_job_err = 1U;
    s_job_active = 0U;
    return;
  }
  s_rx_err = 1U;
}

#define AXI_SRAM_BASE     0x24000000U
#define AXI_SRAM_END      0x24080000U
#define SDRAM_CACHED_BASE 0xD0600000U
#define SDRAM_CACHED_END  0xD0680000U

static int addr_is_cacheable(uint32_t a)
{
  return ((a >= AXI_SRAM_BASE) && (a < AXI_SRAM_END)) ||
         ((a >= SDRAM_CACHED_BASE) && (a < SDRAM_CACHED_END));
}

static void dcache_invalidate(const void *p, uint32_t len)
{
  uint32_t a = (uint32_t)p;
  if (addr_is_cacheable(a))
  {
    SCB_InvalidateDCache_by_Addr((void *)(a & ~31U), (int32_t)(len + (a & 31U)));
  }
}

static int buffer_is_dma_capable(const void *p, uint32_t len)
{
  uint32_t a = (uint32_t)p;

  if ((a & 3U) != 0U)
  {
    return 0;                                  /* IDMA needs word alignment */
  }
  if ((a >= 0x20000000U) && (a < 0x20020000U))
  {
    return 0;                                  /* DTCM: not reachable by IDMA */
  }
  if (addr_is_cacheable(a) && (((a & 31U) != 0U) || ((len & 31U) != 0U)))
  {
    return 0;                                  /* would corrupt neighbouring lines */
  }
  return 1;
}

static int sd_wait_transfer_state(void)
{
  uint32_t t0 = HAL_GetTick();

  while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
  {
    if ((HAL_GetTick() - t0) > SD_TIMEOUT_MS)
    {
      return -1;
    }
  }
  return 0;
}

static int sd_read_dma(uint8_t *dst, uint32_t sector, uint32_t count)
{
  uint32_t t0;

  if (sd_wait_transfer_state() != 0)
  {
    return -1;
  }

  dcache_invalidate(dst, count * SD_SECTOR_SIZE);
  s_rx_done = 0U;
  s_rx_err  = 0U;
  if (HAL_SD_ReadBlocks_DMA(&hsd1, dst, sector, count) != HAL_OK)
  {
    return -2;
  }

  t0 = HAL_GetTick();
  while ((s_rx_done == 0U) && (s_rx_err == 0U))
  {
    if ((HAL_GetTick() - t0) > SD_TIMEOUT_MS)
    {
      (void)HAL_SD_Abort(&hsd1);
      return -3;
    }
  }

  if (s_rx_err != 0U)
  {
    return -4;
  }
  dcache_invalidate(dst, count * SD_SECTOR_SIZE);   /* drop speculative fills */
  return 0;
}

/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
  return (pdrv == 0U) ? s_stat : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  if ((s_stat & STA_NOINIT) != 0U)
  {
    if (MX_SDMMC1_SD_Init() == HAL_OK)
    {
      s_stat &= (DSTATUS)~STA_NOINIT;
    }
  }
  return s_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  if ((pdrv != 0U) || (count == 0U))
  {
    return RES_PARERR;
  }
  if ((s_stat & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }

  if (buffer_is_dma_capable(buff, (uint32_t)count * SD_SECTOR_SIZE))
  {
    return (sd_read_dma(buff, (uint32_t)sector, (uint32_t)count) == 0) ? RES_OK : RES_ERROR;
  }

  /* Slow path: one sector at a time through the bounce buffer */
  for (UINT i = 0; i < count; i++)
  {
    if (sd_read_dma(s_bounce, (uint32_t)sector + i, 1U) != 0)
    {
      return RES_ERROR;
    }
    for (UINT k = 0; k < SD_SECTOR_SIZE; k++)
    {
      buff[(i * SD_SECTOR_SIZE) + k] = s_bounce[k];
    }
  }
  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  HAL_SD_CardInfoTypeDef info;

  if (pdrv != 0U)
  {
    return RES_PARERR;
  }
  if ((s_stat & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }

  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;

    case GET_SECTOR_COUNT:
      (void)HAL_SD_GetCardInfo(&hsd1, &info);
      *(LBA_t *)buff = (LBA_t)info.LogBlockNbr;
      return RES_OK;

    case GET_SECTOR_SIZE:
      *(WORD *)buff = (WORD)SD_SECTOR_SIZE;
      return RES_OK;

    case GET_BLOCK_SIZE:
      (void)HAL_SD_GetCardInfo(&hsd1, &info);
      *(DWORD *)buff = info.LogBlockSize / SD_SECTOR_SIZE;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}

/*-----------------------------------------------------------------------*/
/* Async raw read job API (see sd_diskio.h)                              */
/*-----------------------------------------------------------------------*/
int SD_Job_Busy(void)
{
  return (int)s_job_active;
}

int SD_Job_Start(const sd_seg_t *segs, uint32_t nsegs)
{
  if ((nsegs == 0U) || (nsegs > SD_JOB_MAX_SEGS) || s_job_active)
  {
    return -1;
  }
  if ((s_stat & STA_NOINIT) != 0U)
  {
    return -1;
  }
  for (uint32_t i = 0; i < nsegs; i++)
  {
    if ((segs[i].count == 0U) ||
        !buffer_is_dma_capable(segs[i].buf, segs[i].count * SD_SECTOR_SIZE)) { return -1; }
    s_job[i] = segs[i];
    dcache_invalidate(segs[i].buf, segs[i].count * SD_SECTOR_SIZE);
  }
  if (sd_wait_transfer_state() != 0)
  {
    return -2;
  }

  s_job_n      = nsegs;
  s_job_i      = 0U;
  s_job_err    = 0U;
  s_job_active = 1U;
  if (HAL_SD_ReadBlocks_DMA(&hsd1, s_job[0].buf, s_job[0].lba, s_job[0].count) != HAL_OK)
  {
    s_job_active = 0U;
    return -3;
  }
  return 0;
}

int SD_Job_Wait(uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();

  while (s_job_active)
  {
    if ((HAL_GetTick() - t0) > timeout_ms)
    {
      (void)HAL_SD_Abort(&hsd1);
      s_job_active = 0U;
      return -1;
    }
  }
  if (s_job_err) { return -2; }
  for (uint32_t i = 0; i < s_job_n; i++)                /* drop speculative fills */
  {
    dcache_invalidate(s_job[i].buf, s_job[i].count * SD_SECTOR_SIZE);
  }
  return 0;
}
