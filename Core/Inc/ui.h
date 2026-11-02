#ifndef UI_H
#define UI_H

#include "main.h"

/* Control bar = LTDC layer 2, ARGB4444, alpha-blended over the video */
#define UI_BAR_H        80U
#define UI_BAR_Y        (600U - UI_BAR_H)        /* screen y of bar top */

/* Hit areas in screen coordinates */
#define UI_BTN_X0       0U
#define UI_BTN_X1       120U     /* generous hit area: whole left of bar */
#define UI_SEEK_X0      110U                     /* progress track */
#define UI_SEEK_X1      850U

void     UI_Init(void);
void     UI_SetVisible(int visible);              /* applied at next LTDC reload */
int      UI_IsVisible(void);
void     UI_Draw(int paused, uint32_t frame, uint32_t nframes, uint32_t fps);

/* screen x -> frame index on the seek track */
uint32_t UI_SeekFrameFromX(uint16_t x, uint32_t nframes);

#endif
