#ifndef SDRAM_H
#define SDRAM_H

#include "main.h"

/* IS42S32800J-7BLI on FMC SDRAM Bank2 (SDNE1/SDCKE1) -> 0xD0000000, 32 MB */
#define SDRAM_BASE_ADDR   0xD0000000U
#define SDRAM_SIZE_BYTES  (32U * 1024U * 1024U)

HAL_StatusTypeDef SDRAM_InitSequence(SDRAM_HandleTypeDef *hsdram);

/* Returns -1 if OK, otherwise index (32-bit word) of first failure */
int32_t SDRAM_Test(uint32_t bytes);

#endif
