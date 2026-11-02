/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SD card -> SDRAM -> LTDC raw RGB565 video player
  *
  * Board   : STM32H743XIH6 Core Board V1.1 (HQZY / WKS)
  * Display : 7.0" 1024x600 IPS RGB, EK79001 (Alientek-compatible 40-pin) on LCD1
  * Storage : TF card on SDMMC1 (PC8..PC12, PD2), FatFs R0.15, read-only
  * Memory  : IS42S32800J 32 MB SDRAM on FMC bank 2 @ 0xD0000000
  * Console : USART1 PA9/PA10 -> CH340X "COM" USB-C, 115200 8N1
  *
  * Clocks  : HSE 25 MHz -> PLL1 400 MHz SYSCLK, HCLK 200 MHz, APBx 100 MHz
  *           PLL1Q 200 MHz -> SDMMC kernel, PLL3R 51.2 MHz -> LTDC pixel clock
  *           FMC = HCLK 200 MHz -> SDCLK 100 MHz
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "sdram.h"
#include "video_player.h"
#include "touch.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define VIDEO_FILE   "VIDEO.BIN"
#define VIDEO_W      512U      /* must match ffmpeg scale                    */
#define VIDEO_H      300U
#define VIDEO_FPS    30U       /* must match ffmpeg fps                      */
#define VIDEO_SCALE  2U        /* 2: 512x300 doubled to full 1024x600 screen */
                               /* 1: native size (e.g. 1024x600 file, ~14 fps) */
#define SHOW_COLOR_BARS 0      /* 1: 2 s LTDC test pattern at boot           */
/* USER CODE END PD */

/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
LTDC_HandleTypeDef  hltdc;
SD_HandleTypeDef    hsd1;
UART_HandleTypeDef  huart1;
SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_FMC_Init(void);
static void MX_LTDC_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void fatal_blink(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
/* printf() -> USART1 (used by _write() in syscalls.c) */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart1, &c, 1U, 10U);
  return ch;
}

static void fatal_blink(void)
{
  /* LEDs are active low (common anode to 3V3) */
  HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
  for (;;)
  {
    HAL_GPIO_TogglePin(LED_B_GPIO_Port, LED_B_Pin);
    HAL_Delay(150);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable I-Cache and D-Cache.
     SDRAM (frame buffers, UI) is non-cacheable via MPU region 1, so the LTDC
     always sees CPU writes. IDMA targets in cacheable memory (AXI SRAM and the
     SDRAM source window, MPU region 2) are invalidated in sd_diskio.c. */
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals.
     FMC (with SDRAM JEDEC init) MUST come before LTDC. */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_FMC_Init();
  MX_LTDC_Init();
  /* SDMMC1 is initialised on demand by FatFs (disk_initialize) */

  /* USER CODE BEGIN 2 */
  printf("\r\n=== H743 SD video player ===\r\n");
#if defined(__OPTIMIZE__)
  printf("Build: optimised, I+D cache on\r\n");
#else
  printf("Build: Debug -O0 (hot loops forced -O3), I+D cache on\r\n");
#endif
  printf("SYSCLK %lu MHz, HCLK %lu MHz\r\n",
         (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000U),
         (unsigned long)(HAL_RCC_GetHCLKFreq() / 1000000U));

  /* 1. SDRAM check: 1 MB pattern + full 32 MB address-line test */
  {
    int32_t bad = SDRAM_Test(1024U * 1024U);
    if (bad >= 0)
    {
      printf("SDRAM FAIL at word %ld (try ReadPipeDelay 0/2)\r\n", (long)bad);
      fatal_blink();
    }
    printf("SDRAM OK\r\n");
  }

  /* 2. Panel reset (LCD1 pin 40) + backlight on (LCD1 pin 34) */
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);

#if SHOW_COLOR_BARS
  /* 3. 2 s colour bars with red border: verifies LTDC timing and wiring */
  LCD_ColorBars();
  HAL_Delay(2000);
#endif

  /* 4. Capacitive touch (FT5x06 / GT911 auto-detect) */
  (void)Touch_Init();

  /* 5. Play forever with touch controls; only returns on error */
  {
    int rc = Video_Play(VIDEO_FILE, VIDEO_W, VIDEO_H, VIDEO_FPS, VIDEO_SCALE);
    printf("Video_Play error %d\r\n", rc);
    fatal_blink();
  }
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *        HSE 25 MHz / M5 = 5 MHz * N160 = 800 MHz VCO
  *        P/2 = 400 MHz SYSCLK, Q/4 = 200 MHz (SDMMC), R/2 = 400 MHz (unused)
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /* 400 MHz requires VOS1 */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 5;
  RCC_OscInitStruct.PLL.PLLN       = 160;
  RCC_OscInitStruct.PLL.PLLP       = 2;
  RCC_OscInitStruct.PLL.PLLQ       = 4;
  RCC_OscInitStruct.PLL.PLLR       = 2;
  RCC_OscInitStruct.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;   /* 4..8 MHz input */
  RCC_OscInitStruct.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN   = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                   | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* 400 MHz */
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;     /* 200 MHz */
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;     /* 100 MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  *        PLL3: 1 MHz * 256 = 256 MHz, R/5 = 51.2 MHz LTDC pixel clock
  *        1344 x 635 total -> 59.99 Hz, so 30 fps = exactly 2 refreshes/frame
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC | RCC_PERIPHCLK_SDMMC
                                           | RCC_PERIPHCLK_FMC  | RCC_PERIPHCLK_USART1;
  PeriphClkInitStruct.PLL3.PLL3M      = 25;     /* 1 MHz reference          */
  PeriphClkInitStruct.PLL3.PLL3N      = 256;    /* 256 MHz VCO (medium)     */
  PeriphClkInitStruct.PLL3.PLL3P      = 2;
  PeriphClkInitStruct.PLL3.PLL3Q      = 2;
  PeriphClkInitStruct.PLL3.PLL3R      = 5;      /* 51.2 MHz -> 60.0 Hz refresh */
  PeriphClkInitStruct.PLL3.PLL3RGE    = RCC_PLL3VCIRANGE_0;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN  = 0;
  PeriphClkInitStruct.FmcClockSelection     = RCC_FMCCLKSOURCE_D1HCLK;
  PeriphClkInitStruct.SdmmcClockSelection   = RCC_SDMMCCLKSOURCE_PLL;
  PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LTDC Initialization: 1024x600, HSW 20 HBP 140 HFP 160, VSW 3 VBP 20 VFP 12
  *        No layer is configured here -> LTDC shows black background and does
  *        not fetch SDRAM until video_player.c configures layer 1.
  */
static void MX_LTDC_Init(void)
{
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity         = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity         = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity         = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity         = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync     = 20U - 1U;                    /* 19   */
  hltdc.Init.VerticalSync       = 3U - 1U;                     /* 2    */
  hltdc.Init.AccumulatedHBP     = 20U + 140U - 1U;             /* 159  */
  hltdc.Init.AccumulatedVBP     = 3U + 20U - 1U;               /* 22   */
  hltdc.Init.AccumulatedActiveW = 20U + 140U + 1024U - 1U;     /* 1183 */
  hltdc.Init.AccumulatedActiveH = 3U + 20U + 600U - 1U;        /* 622  */
  hltdc.Init.TotalWidth         = 20U + 140U + 1024U + 160U - 1U; /* 1343 */
  hltdc.Init.TotalHeigh         = 3U + 20U + 600U + 12U - 1U;     /* 634  */
  hltdc.Init.Backcolor.Blue     = 0;
  hltdc.Init.Backcolor.Green    = 0;
  hltdc.Init.Backcolor.Red      = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SDMMC1 Initialization (4-bit, 25 MHz, then High Speed 50 MHz if card supports it)
  * @note  Called from FatFs disk_initialize(); returns error instead of
  *        trapping so a missing card is reported on the console.
  */
HAL_StatusTypeDef MX_SDMMC1_SD_Init(void)
{
  hsd1.Instance                 = SDMMC1;
  hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd1.Init.ClockDiv            = 4;       /* 200 MHz / (2*4) = 25 MHz */
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    return HAL_ERROR;
  }

  /* USER CODE BEGIN SDMMC1_Init 2 */
  /* Switch card to High Speed (CMD6) and raise SDMMC_CK to 50 MHz.
     If the card refuses, stay at 25 MHz default speed. */
  if (HAL_SD_ConfigSpeedBusOperation(&hsd1, SDMMC_SPEED_MODE_HIGH) == HAL_OK)
  {
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV, 2U);  /* 50 MHz */
    printf("SD: high speed, 50 MHz\r\n");
  }
  else
  {
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State     = HAL_SD_STATE_READY;
    printf("SD: default speed, 25 MHz\r\n");
  }
  /* USER CODE END SDMMC1_Init 2 */

  return HAL_OK;
}

/**
  * @brief USART1 Initialization: 115200 8N1 (PA9 TX, PA10 RX -> CH340X)
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance                    = USART1;
  huart1.Init.BaudRate               = 115200;
  huart1.Init.WordLength             = UART_WORDLENGTH_8B;
  huart1.Init.StopBits               = UART_STOPBITS_1;
  huart1.Init.Parity                 = UART_PARITY_NONE;
  huart1.Init.Mode                   = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FMC SDRAM bank 2: IS42S32800J-7BLI, 4 banks, 12 row, 9 col, 32 bit
  *        SDCLK = 100 MHz, CL3. Timings in SDCLK cycles (10 ns).
  */
static void MX_FMC_Init(void)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  hsdram1.Instance                = FMC_SDRAM_DEVICE;
  hsdram1.Init.SDBank             = FMC_SDRAM_BANK2;
  hsdram1.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_32;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_1;

  SdramTiming.LoadToActiveDelay    = 2;   /* tMRD  2 clk          */
  SdramTiming.ExitSelfRefreshDelay = 7;   /* tXSR  70 ns          */
  SdramTiming.SelfRefreshTime      = 5;   /* tRAS  42 ns          */
  SdramTiming.RowCycleDelay        = 7;   /* tRC   63 ns          */
  SdramTiming.WriteRecoveryTime    = 3;   /* >= tRAS-tRCD, tRC-tRCD-tRP */
  SdramTiming.RPDelay              = 2;   /* tRP   20 ns          */
  SdramTiming.RCDDelay             = 2;   /* tRCD  20 ns          */

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN FMC_Init 2 */
  if (SDRAM_InitSequence(&hsdram1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END FMC_Init 2 */
}

/**
  * @brief GPIO Initialization (plain GPIO only; AF pins are in the MSP file)
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();

  /* Initial levels: backlight off, LCD out of reset, LEDs off */
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = LED_G_Pin;
  HAL_GPIO_Init(LED_G_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LCD_BL_Pin | LED_B_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LCD_RST_Pin;
  HAL_GPIO_Init(LCD_RST_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief MPU configuration
  *  Region 0: CubeMX default background, blocks speculative access to
  *            0x6000_0000..0xDFFF_FFFF (FMC NOR/QSPI/SDRAM windows).
  *  Region 1: SDRAM 0xD000_0000, 32 MB, Normal memory, non-cacheable,
  *            full access (overrides region 0; unaligned access allowed).
  *  Region 2: SDRAM 0xD060_0000, 512 KB, cacheable write-through (video src[1]).
  */
static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress      = 0x0;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress      = 0xD0000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Region 2: SDRAM 0xD060_0000, 512 KB = second video source buffer.
     Cacheable write-through, no write-allocate: CPU only reads it, so line
     fills give 32-byte bursts; invalidated after each IDMA transfer. */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress      = 0xD0600000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Blue LED fast blink without relying on SysTick */
  __disable_irq();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  while (1)
  {
    HAL_GPIO_TogglePin(LED_B_GPIO_Port, LED_B_Pin);
    for (volatile uint32_t i = 0; i < 4000000U; i++) { }
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
