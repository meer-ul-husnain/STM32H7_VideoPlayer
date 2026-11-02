#ifndef SD_DISKIO_H
#define SD_DISKIO_H

#include "main.h"

/*
 * Asynchronous raw sector reads for the video path (bypasses FatFs).
 * A job is a list of contiguous LBA segments; each segment is one CMD18
 * multi-block IDMA transfer, chained from the SDMMC1 completion interrupt.
 * Do not call FatFs while a job is active.
 */
#define SD_JOB_MAX_SEGS  8U

typedef struct
{
  uint32_t lba;
  uint32_t count;     /* 512-byte sectors */
  uint8_t *buf;       /* 4-byte aligned, AXI SRAM or SDRAM (not DTCM) */
} sd_seg_t;

int SD_Job_Start(const sd_seg_t *segs, uint32_t nsegs);   /* 0 = started   */
int SD_Job_Wait(uint32_t timeout_ms);                     /* 0 = complete  */
int SD_Job_Busy(void);

#endif
