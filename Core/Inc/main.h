/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   STM32H743XIH6 Core Board V1.1 + 7" 1024x600 RGB LCD
  *                   SD card raw RGB565 video player.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* USER CODE BEGIN EM */

/* USER CODE END EM */

void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* SDMMC1 + card init, called from FatFs disk_initialize() (sd_diskio.c) */
HAL_StatusTypeDef MX_SDMMC1_SD_Init(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_G_Pin          GPIO_PIN_3      /* tri-colour LED green, active low */
#define LED_G_GPIO_Port    GPIOA
#define LCD_BL_Pin         GPIO_PIN_0      /* LCD1 pin 34 BL_CTR, active high  */
#define LCD_BL_GPIO_Port   GPIOB
#define LED_B_Pin          GPIO_PIN_1      /* tri-colour LED blue, active low  */
#define LED_B_GPIO_Port    GPIOB
#define LCD_RST_Pin        GPIO_PIN_5      /* LCD1 pin 40 LCD_RST, active low  */
#define LCD_RST_GPIO_Port  GPIOH

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
