#include "sdram.h"

#define SDRAM_TIMEOUT                         0xFFFFU

/* JEDEC mode register */
#define SDRAM_MODEREG_BURST_LENGTH_1          0x0000U
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   0x0000U
#define SDRAM_MODEREG_CAS_LATENCY_3           0x0030U
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD 0x0000U
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  0x0200U

/* Refresh: 64 ms / 4096 rows = 15.625 us.
 * SDCLK = HCLK/2 = 100 MHz -> 15.625 us * 100 MHz = 1562, minus 20 margin. */
#define SDRAM_REFRESH_COUNT                   1542U

HAL_StatusTypeDef SDRAM_InitSequence(SDRAM_HandleTypeDef *hsdram)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;

    /* 1. Clock enable */
    cmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;
    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) return HAL_ERROR;

    /* 2. >= 100 us power-up delay */
    HAL_Delay(1);

    /* 3. Precharge all */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) return HAL_ERROR;

    /* 4. 8x auto-refresh */
    cmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;
    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) return HAL_ERROR;

    /* 5. Load mode register: BL=1, sequential, CL=3, single write burst */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = SDRAM_MODEREG_BURST_LENGTH_1 |
                                 SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
                                 SDRAM_MODEREG_CAS_LATENCY_3 |
                                 SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                                 SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    if (HAL_SDRAM_SendCommand(hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) return HAL_ERROR;

    /* 6. Refresh rate */
    return HAL_SDRAM_ProgramRefreshRate(hsdram, SDRAM_REFRESH_COUNT);
}

int32_t SDRAM_Test(uint32_t bytes)
{
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE_ADDR;
    uint32_t n = bytes / 4U;

    if (bytes > SDRAM_SIZE_BYTES) n = SDRAM_SIZE_BYTES / 4U;

    /* Data/pattern test */
    for (uint32_t i = 0; i < n; i++) p[i] = i ^ 0xA5A5A5A5U;
    for (uint32_t i = 0; i < n; i++) if (p[i] != (i ^ 0xA5A5A5A5U)) return (int32_t)i;

    /* Address-line test across full 32 MB (powers of two) */
    for (uint32_t a = 1; a < SDRAM_SIZE_BYTES / 4U; a <<= 1) p[a] = a;
    p[0] = 0xDEADBEEFU;
    for (uint32_t a = 1; a < SDRAM_SIZE_BYTES / 4U; a <<= 1) if (p[a] != a) return (int32_t)a;

    return -1;
}
