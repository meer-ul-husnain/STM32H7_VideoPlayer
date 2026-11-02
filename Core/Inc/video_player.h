#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "main.h"

/* Alientek-type 7" 1024x600 RGB (EK79001) */
#define LCD_WIDTH   1024U
#define LCD_HEIGHT  600U

/* Return codes */
#define VP_ERR_SIZE    (-1)
#define VP_ERR_MOUNT   (-2)
#define VP_ERR_OPEN    (-3)
#define VP_ERR_READ    (-4)
#define VP_ERR_EMPTY   (-5)

/* Full-screen colour bars + red 1-px border: verifies LTDC timing/wiring */
void LCD_ColorBars(void);

/*
 * Plays a raw RGB565LE file (no header) in a loop with touch controls.
 *   w, h  : frame size stored in the file
 *   fps   : playback rate
 *   scale : 1 = native size (centred), 2 = pixel-doubled (512x300 -> 1024x600)
 * Touch: tap video = show/hide control bar; bar button = play/pause;
 *        tap or drag on the seek bar = seek (applied on release).
 * Only returns on error.
 */
int Video_Play(const char *path, uint32_t w, uint32_t h, uint32_t fps, uint32_t scale);

#endif
