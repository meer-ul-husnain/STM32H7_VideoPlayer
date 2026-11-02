#ifndef TOUCH_H
#define TOUCH_H

#include "main.h"

/*
 * Capacitive touch on LCD1 (Alientek-compatible 40-pin RGB connector)
 *   PB10  LCD1 pin 38  CTP_SCL   (software I2C, open-drain)
 *   PB11  LCD1 pin 36  CTP_SDA   (software I2C, open-drain)
 *   PB12  LCD1 pin 35  CTP_RST
 *   PB5   LCD1 pin 39  CTP_INT
 * Controller auto-detected: FocalTech FT5x06 family (FT5206/FT5426/FT5446, 0x38)
 * or Goodix GT911/GT9147 (0x5D / 0x14).
 */

/* ---- Orientation / calibration, per controller ---------------------------
 * The FT5x06 on the Alientek 7" 1024x600 module reports in panel-native
 * portrait order (first coordinate = screen Y 0..599, second = screen X 0..1023),
 * so X/Y must be swapped for landscape. (Unswapped, a tap at the top-right
 * lands at bottom-left = on the play/pause button.)
 * If taps are still mirrored: set TOUCH_DEBUG 1, touch the four corners, read
 * the raw values on the COM port and flip the matching INVERT flag. */
#define TOUCH_FT_SWAP_XY     1
#define TOUCH_FT_INVERT_X    0
#define TOUCH_FT_INVERT_Y    0

#define TOUCH_GT_SWAP_XY     0
#define TOUCH_GT_INVERT_X    0
#define TOUCH_GT_INVERT_Y    0

/* Raw coordinate range AFTER the swap (landscape) */
#define TOUCH_RAW_MAX_X      1024U
#define TOUCH_RAW_MAX_Y      600U

#define TOUCH_DEBUG          0

typedef enum
{
  TOUCH_NONE = 0,
  TOUCH_FT5X06,
  TOUCH_GT911
} touch_chip_t;

/* Returns detected controller (TOUCH_NONE if nothing answered) */
touch_chip_t Touch_Init(void);

/* Returns 1 while a finger is down; x/y in LCD pixels (0..1023, 0..599) */
int Touch_Read(uint16_t *x, uint16_t *y);

#endif
