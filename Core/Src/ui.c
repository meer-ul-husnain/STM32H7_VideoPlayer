/**
  * @file  ui.c
  * @brief On-screen control bar: play/pause button, seek bar, mm:ss / mm:ss.
  *        Rendered by CPU into an ARGB4444 buffer in SDRAM shown on LTDC layer 2.
  */
#include "ui.h"
#include "sdram.h"

extern LTDC_HandleTypeDef hltdc;

#define UI_W          1024U
#define UI_FB_ADDR    (SDRAM_BASE_ADDR + 0x00400000U)   /* 160 KB */

/* ARGB4444 colours */
#define C_CLEAR       0x0000U
#define C_BG          0xB000U     /* black, ~70 % opaque */
#define C_WHITE       0xFFFFU
#define C_TRACK       0xF555U
#define C_FILL        0xFF80U     /* orange */

/* Layout inside the bar (bar-local coordinates) */
#define TRACK_Y0      36U
#define TRACK_H       8U
#define KNOB_R        11
#define ICON_X        30U
#define ICON_Y        20U
#define ICON_H        40U
#define TIME_X        866U
#define TIME_Y        33U

static uint16_t *const s_fb = (uint16_t *)UI_FB_ADDR;
static int s_visible;

/* 5x7 glyphs: '0'..'9', ':', '/' */
static const uint8_t s_font[12][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, {0x01,0x01,0x02,0x04,0x08,0x10,0x10}
};

static void fill_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint16_t c)
{
  for (uint32_t y = y0; (y < y0 + h) && (y < UI_BAR_H); y++)
  {
    uint16_t *p = &s_fb[y * UI_W];
    for (uint32_t x = x0; (x < x0 + w) && (x < UI_W); x++)
    {
      p[x] = c;
    }
  }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
  for (int dy = -r; dy <= r; dy++)
  {
    for (int dx = -r; dx <= r; dx++)
    {
      int x = cx + dx, y = cy + dy;
      if (((dx * dx) + (dy * dy) <= (r * r)) &&
          (x >= 0) && (x < (int)UI_W) && (y >= 0) && (y < (int)UI_BAR_H))
      {
        s_fb[(uint32_t)y * UI_W + (uint32_t)x] = c;
      }
    }
  }
}

static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t scale, uint16_t c)
{
  int idx;

  if ((ch >= '0') && (ch <= '9')) { idx = ch - '0'; }
  else if (ch == ':')             { idx = 10; }
  else if (ch == '/')             { idx = 11; }
  else                            { return; }

  for (uint32_t r = 0; r < 7U; r++)
  {
    for (uint32_t col = 0; col < 5U; col++)
    {
      if ((s_font[idx][r] & (0x10U >> col)) != 0U)
      {
        fill_rect(x + col * scale, y + r * scale, scale, scale, c);
      }
    }
  }
}

static void draw_text(uint32_t x, uint32_t y, const char *s, uint32_t scale, uint16_t c)
{
  while (*s != '\0')
  {
    draw_char(x, y, *s, scale, c);
    x += 6U * scale;
    s++;
  }
}

static void fmt_mmss(char *out, uint32_t sec)
{
  uint32_t m = (sec / 60U) % 100U, s = sec % 60U;
  out[0] = (char)('0' + (m / 10U));
  out[1] = (char)('0' + (m % 10U));
  out[2] = ':';
  out[3] = (char)('0' + (s / 10U));
  out[4] = (char)('0' + (s % 10U));
  out[5] = '\0';
}

void UI_Init(void)
{
  LTDC_LayerCfgTypeDef l = {0};

  for (uint32_t i = 0; i < UI_W * UI_BAR_H; i++) { s_fb[i] = C_CLEAR; }

  l.WindowX0        = 0U;
  l.WindowX1        = UI_W;
  l.WindowY0        = UI_BAR_Y;
  l.WindowY1        = UI_BAR_Y + UI_BAR_H;
  l.PixelFormat     = LTDC_PIXEL_FORMAT_ARGB4444;
  l.Alpha           = 255;
  l.Alpha0          = 0;
  l.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  l.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  l.FBStartAdress   = UI_FB_ADDR;
  l.ImageWidth      = UI_W;
  l.ImageHeight     = UI_BAR_H;
  (void)HAL_LTDC_ConfigLayer(&hltdc, &l, LTDC_LAYER_2);

  __HAL_LTDC_LAYER_DISABLE(&hltdc, LTDC_LAYER_2);
  __HAL_LTDC_RELOAD_IMMEDIATE_CONFIG(&hltdc);
  s_visible = 0;
}

void UI_SetVisible(int visible)
{
  s_visible = visible;
  if (visible) { __HAL_LTDC_LAYER_ENABLE(&hltdc, LTDC_LAYER_2); }
  else         { __HAL_LTDC_LAYER_DISABLE(&hltdc, LTDC_LAYER_2); }
}

int UI_IsVisible(void)
{
  return s_visible;
}

uint32_t UI_SeekFrameFromX(uint16_t x, uint32_t nframes)
{
  uint32_t pos;

  if (nframes == 0U)       { return 0U; }
  if (x <= UI_SEEK_X0)     { return 0U; }
  if (x >= UI_SEEK_X1)     { return nframes - 1U; }
  pos = ((uint32_t)(x - UI_SEEK_X0) * nframes) / (UI_SEEK_X1 - UI_SEEK_X0);
  return (pos >= nframes) ? (nframes - 1U) : pos;
}

void UI_Draw(int paused, uint32_t frame, uint32_t nframes, uint32_t fps)
{
  char     t[12];
  uint32_t fill_w, knob_x;
  const uint32_t track_w = UI_SEEK_X1 - UI_SEEK_X0;

  if (nframes == 0U) { nframes = 1U; }
  if (fps == 0U)     { fps = 1U; }
  if (frame >= nframes) { frame = nframes - 1U; }

  fill_rect(0U, 0U, UI_W, UI_BAR_H, C_BG);

  /* play / pause icon: shows the action a tap will perform */
  if (paused)
  {
    for (uint32_t r = 0; r < ICON_H; r++)          /* right-pointing triangle */
    {
      uint32_t len = (r < ICON_H / 2U) ? r : (ICON_H - 1U - r);
      fill_rect(ICON_X + 4U, ICON_Y + r, (len * 17U) / 10U + 1U, 1U, C_WHITE);
    }
  }
  else
  {
    fill_rect(ICON_X + 4U,  ICON_Y, 12U, ICON_H, C_WHITE);   /* || */
    fill_rect(ICON_X + 26U, ICON_Y, 12U, ICON_H, C_WHITE);
  }

  /* seek track */
  fill_w = (uint32_t)(((uint64_t)frame * track_w) / nframes);
  fill_rect(UI_SEEK_X0, TRACK_Y0, track_w, TRACK_H, C_TRACK);
  fill_rect(UI_SEEK_X0, TRACK_Y0, fill_w, TRACK_H, C_FILL);
  knob_x = UI_SEEK_X0 + fill_w;
  fill_circle((int)knob_x, (int)(TRACK_Y0 + TRACK_H / 2U), KNOB_R, C_WHITE);

  /* time: mm:ss/mm:ss */
  fmt_mmss(&t[0], frame / fps);
  t[5] = '/';
  fmt_mmss(&t[6], nframes / fps);
  draw_text(TIME_X, TIME_Y - 7U, t, 2U, C_WHITE);
}
