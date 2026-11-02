/*
 * Raw RGB565 video player with touch controls, 30 fps pipeline.
 *
 * At open, FatFs fast-seek builds the file's cluster map once; after that
 * frames are read as raw LBA ranges with asynchronous SDMMC1 IDMA
 * (sd_diskio.c SD_Job_*), bypassing FatFs' per-cluster splitting.
 *
 * Pipeline (scale 2):
 *     SD IDMA : read frame N+1 -> src[B]      (background)
 *     CPU     : upscale frame N src[A] -> fb  (~7 ms)
 *     LTDC    : flip at VBlank (non-blocking)
 *   frame period = max(SD read, upscale) instead of their sum.
 * Fallback if the file is too fragmented for the map: synchronous f_read().
 * LTDC layer 2 (ui.c) carries the control bar, alpha-blended on top.
 */
#include "video_player.h"
#include "sdram.h"
#include "touch.h"
#include "ui.h"
#include "ff.h"
#include "sd_diskio.h"
#include <string.h>
#include <stdio.h>

extern LTDC_HandleTypeDef hltdc;

#define FB0_ADDR        (SDRAM_BASE_ADDR)
#define FB1_ADDR        (SDRAM_BASE_ADDR + 0x00200000U)   /* 2 MB each */
#define UI_HIDE_MS      3000U
#define SRC_MAX_BYTES   ((LCD_WIDTH / 2U) * (LCD_HEIGHT / 2U) * 2U)   /* 307200 */

static FATFS s_fs;          /* AXI SRAM (RAM_D1): reachable by SDMMC1 IDMA */
static FIL   s_fil;
static volatile uint8_t s_reload_done;

#define SRC1_ADDR       (SDRAM_BASE_ADDR + 0x00600000U)   /* 2nd source buffer */
#define CLMT_ENTRIES    64U                               /* up to 31 fragments */
#define MAX_FRAGS       ((CLMT_ENTRIES - 2U) / 2U)

/* Source buffers for scale-2 mode (both IDMA-reachable):
   src[0] AXI SRAM (fastest CPU reads), src[1] SDRAM */
static uint32_t s_src0[SRC_MAX_BYTES / 4U] __attribute__((aligned(32)));
static DWORD    s_clmt[CLMT_ENTRIES];

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *h)
{
  (void)h;
  s_reload_done = 1U;
}

static HAL_StatusTypeDef layer_config(uint32_t w, uint32_t h, uint32_t fb)
{
  LTDC_LayerCfgTypeDef l = {0};

  l.WindowX0        = (LCD_WIDTH  - w) / 2U;
  l.WindowX1        = l.WindowX0 + w;
  l.WindowY0        = (LCD_HEIGHT - h) / 2U;
  l.WindowY1        = l.WindowY0 + h;
  l.PixelFormat     = LTDC_PIXEL_FORMAT_RGB565;
  l.Alpha           = 255;
  l.Alpha0          = 0;
  l.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  l.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  l.FBStartAdress   = fb;
  l.ImageWidth      = w;
  l.ImageHeight     = h;
  l.Backcolor.Red   = 0;
  l.Backcolor.Green = 0;
  l.Backcolor.Blue  = 0;

  return HAL_LTDC_ConfigLayer(&hltdc, &l, LTDC_LAYER_1);
}

/*
 * Non-blocking page flip.
 * flip_request() queues the new layer-1 address for the next VBlank and returns
 * immediately, so the SD read of the next frame overlaps the wait.
 * wait_flip_done() must be called before the CPU writes into a frame buffer
 * that may still be on screen.
 */
static volatile uint8_t s_reload_pending;

static void wait_flip_done(void)
{
  uint32_t t;

  if (!s_reload_pending) { return; }
  t = HAL_GetTick();
  while (!s_reload_done)
  {
    if ((HAL_GetTick() - t) > 50U) { break; }       /* LTDC IRQ missing? */
  }
  s_reload_pending = 0U;
}

static void commit_request(void)                    /* apply UI layer changes */
{
  if (s_reload_pending) { return; }                 /* already queued */
  s_reload_done    = 0U;
  s_reload_pending = 1U;
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
}

static void flip_request(uint32_t fb)
{
  wait_flip_done();
  HAL_LTDC_SetAddress_NoReload(&hltdc, fb, LTDC_LAYER_1);
  commit_request();
}

/*
 * 2x nearest-neighbour upscale, 512x300 -> 1024x600.
 *  - Source line (cached: AXI SRAM or write-through SDRAM window) is expanded
 *    into a cached AXI line buffer.
 *  - That line is written to the two destination rows one after the other,
 *    so every SDRAM store hits the open row (a 1024-px RGB565 line is exactly
 *    one 2 KB SDRAM row). Interleaving the two rows, as before, forced a
 *    precharge + activate on almost every store.
 *  - Forced -O3 so a Debug (-O0) build still runs it at full speed.
 */
static uint32_t s_line[LCD_WIDTH / 2U] __attribute__((aligned(32)));

__attribute__((optimize("O3")))
static void store_line(uint32_t *restrict d, const uint32_t *restrict l, uint32_t words)
{
  for (uint32_t i = 0; i < words; i += 8U)
  {
    uint32_t a0 = l[i],      a1 = l[i + 1U], a2 = l[i + 2U], a3 = l[i + 3U];
    uint32_t a4 = l[i + 4U], a5 = l[i + 5U], a6 = l[i + 6U], a7 = l[i + 7U];
    d[i]      = a0; d[i + 1U] = a1; d[i + 2U] = a2; d[i + 3U] = a3;
    d[i + 4U] = a4; d[i + 5U] = a5; d[i + 6U] = a6; d[i + 7U] = a7;
  }
}

__attribute__((optimize("O3")))
static void upscale2x(const uint32_t *src, uint32_t fb, uint32_t sw, uint32_t sh)
{
  const uint32_t words_in  = sw / 2U;       /* 2 px per word            */
  const uint32_t words_out = sw;            /* 2*sw px per output line  */
  uint32_t *dst = (uint32_t *)fb;

  for (uint32_t y = 0; y < sh; y++)
  {
    const uint32_t *s = &src[y * words_in];
    uint32_t *d0 = &dst[(2U * y) * words_out];

    for (uint32_t x = 0; x < words_in; x++)
    {
      uint32_t p = s[x];
      s_line[2U * x]      = (p & 0xFFFFU) * 0x00010001U;
      s_line[2U * x + 1U] = (p >> 16)     * 0x00010001U;
    }
    store_line(d0, s_line, words_out);
    store_line(d0 + words_out, s_line, words_out);
  }
}

void LCD_ColorBars(void)
{
  static const uint16_t bars[8] = {
    0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
  };
  uint16_t *p = (uint16_t *)FB0_ADDR;

  for (uint32_t y = 0; y < LCD_HEIGHT; y++)
    for (uint32_t x = 0; x < LCD_WIDTH; x++)
      p[y * LCD_WIDTH + x] = bars[(x * 8U) / LCD_WIDTH];

  for (uint32_t x = 0; x < LCD_WIDTH; x++)
  {
    p[x] = 0xF800;
    p[(LCD_HEIGHT - 1U) * LCD_WIDTH + x] = 0xF800;
  }
  for (uint32_t y = 0; y < LCD_HEIGHT; y++)
  {
    p[y * LCD_WIDTH] = 0xF800;
    p[y * LCD_WIDTH + LCD_WIDTH - 1U] = 0xF800;
  }

  layer_config(LCD_WIDTH, LCD_HEIGHT, FB0_ADDR);
}

/* ------------------------------------------------------------------------- */
typedef struct
{
  uint32_t file_cl;         /* first cluster index within the file */
  uint32_t ncl;             /* fragment length [clusters]          */
  uint32_t start_cl;        /* first cluster number on the volume  */
} frag_t;

typedef struct
{
  /* media */
  uint32_t w, h, fps, scale;
  uint32_t frame_bytes, frame_secs;
  uint32_t nframes;
  uint32_t fb[2];
  uint32_t back;
  uint32_t shown;           /* frame index on screen */
  uint8_t *src[2];

  /* raw-LBA access (fast path) */
  int      raw;             /* 1 = cluster map valid */
  frag_t   frag[MAX_FRAGS];
  uint32_t nfrag;

  /* prefetch */
  int      pf_active;       /* job in flight or data ready */
  uint32_t pf_frame;
  uint32_t pf_idx;          /* which src[] / fb it targets  */

  /* input / UI */
  int      paused, dragging, touch_down, ui_dirty;
  uint8_t  up_count;
  uint32_t drag_frame, last_touch_ms, last_scrub_ms, last_ui_ms;

  /* timing */
  uint32_t t0, run;
  uint32_t stat_t, stat_n, sd_wait_ms, up_ms;
} player_t;

#define RELEASE_DEBOUNCE   2U
#define SCRUB_PERIOD_MS    60U
#define SD_JOB_TIMEOUT_MS  500U

/* ---------------- cluster map -> LBA segments ---------------- */
static int build_frag_table(player_t *p)
{
  uint32_t file_cl = 0U;
  DWORD   *t;

  s_clmt[0] = CLMT_ENTRIES;
  s_fil.cltbl = s_clmt;
  if (f_lseek(&s_fil, CREATE_LINKMAP) != FR_OK)
  {
    s_fil.cltbl = NULL;                     /* too fragmented: FatFs path */
    return -1;
  }

  p->nfrag = 0U;
  t = &s_clmt[1];
  while ((t[0] != 0U) && (p->nfrag < MAX_FRAGS))
  {
    p->frag[p->nfrag].file_cl  = file_cl;
    p->frag[p->nfrag].ncl      = t[0];
    p->frag[p->nfrag].start_cl = t[1];
    file_cl += t[0];
    p->nfrag++;
    t += 2;
  }
  return (p->nfrag > 0U) ? 0 : -1;
}

/* Build IDMA segments for one frame. Returns number of segments or <0. */
static int frame_segments(const player_t *p, uint32_t frame, uint8_t *buf, sd_seg_t *seg)
{
  const uint32_t csize = s_fs.csize;
  uint32_t s   = frame * p->frame_secs;        /* sector offset within file */
  uint32_t rem = p->frame_secs;
  int      n   = 0;

  while (rem > 0U)
  {
    uint32_t ci = s / csize, so = s % csize, k;
    uint32_t avail, take;

    for (k = 0; k < p->nfrag; k++)
    {
      if ((ci >= p->frag[k].file_cl) && (ci < (p->frag[k].file_cl + p->frag[k].ncl))) { break; }
    }
    if ((k == p->nfrag) || (n >= (int)SD_JOB_MAX_SEGS)) { return -1; }

    avail = ((p->frag[k].file_cl + p->frag[k].ncl - ci) * csize) - so;
    take  = (rem < avail) ? rem : avail;

    seg[n].lba   = (uint32_t)s_fs.database +
                   ((p->frag[k].start_cl + (ci - p->frag[k].file_cl) - 2U) * csize) + so;
    seg[n].count = take;
    seg[n].buf   = buf;
    n++;

    buf += take * 512U;
    s   += take;
    rem -= take;
  }
  return n;
}

/* Start reading `frame` into buf. Raw path: async. FatFs path: synchronous. */
static int read_start(player_t *p, uint32_t frame, uint8_t *buf)
{
  if (p->raw)
  {
    sd_seg_t seg[SD_JOB_MAX_SEGS];
    int n = frame_segments(p, frame, buf, seg);
    if ((n > 0) && (SD_Job_Start(seg, (uint32_t)n) == 0)) { return 0; }
    printf("raw read failed, switching to FatFs path\r\n");
    p->raw = 0;
    s_fil.cltbl = NULL;
  }
  {
    UINT br;
    if (f_lseek(&s_fil, (FSIZE_t)frame * p->frame_bytes) != FR_OK) { return VP_ERR_READ; }
    if ((f_read(&s_fil, buf, p->frame_bytes, &br) != FR_OK) || (br != p->frame_bytes))
    {
      return VP_ERR_READ;
    }
  }
  return 0;
}

static int read_wait(player_t *p)
{
  uint32_t t = HAL_GetTick();
  int rc = 0;

  if (p->raw)
  {
    rc = SD_Job_Wait(SD_JOB_TIMEOUT_MS);
  }
  p->sd_wait_ms += HAL_GetTick() - t;
  return (rc == 0) ? 0 : VP_ERR_READ;
}

/* Drop any prefetch (before seek / scrub). */
static int prefetch_cancel(player_t *p)
{
  int rc = 0;
  if (p->pf_active) { rc = read_wait(p); p->pf_active = 0; }
  return rc;
}

/* Destination for a source read: scale 2 -> src[idx], scale 1 -> fb directly */
static uint8_t *read_dst(player_t *p, uint32_t idx)
{
  return (p->scale == 2U) ? p->src[idx] : (uint8_t *)p->fb[p->back];
}

/* Make the frame in src[idx] (or fb) visible at next VBlank */
static void render_and_present(player_t *p, uint32_t idx, uint32_t frame)
{
  if (p->scale == 2U)
  {
    uint32_t t;
    wait_flip_done();                         /* back buffer off screen */
    t = HAL_GetTick();
    upscale2x((const uint32_t *)p->src[idx], p->fb[p->back], p->w, p->h);
    p->up_ms += HAL_GetTick() - t;
  }
  flip_request(p->fb[p->back]);
  p->back ^= 1U;
  p->shown = frame;
}

/* Synchronous: read + show one frame (seek preview, pause seek) */
static int show_frame_now(player_t *p, uint32_t frame)
{
  int rc;

  if (frame >= p->nframes) { frame = p->nframes - 1U; }
  if ((rc = prefetch_cancel(p)) != 0) { return rc; }
  if (p->scale == 1U) { wait_flip_done(); }
  if ((rc = read_start(p, frame, read_dst(p, 0U))) != 0) { return rc; }
  if ((rc = read_wait(p)) != 0) { return rc; }
  render_and_present(p, 0U, frame);
  return 0;
}

static void restart_clock(player_t *p)
{
  p->t0  = HAL_GetTick();
  p->run = 0U;
}

/* ---------------- touch ---------------- */
static int handle_input(player_t *p)
{
  uint16_t tx = 0U, ty = 0U;
  uint32_t now = HAL_GetTick();
  int raw = Touch_Read(&tx, &ty);
  int rc;

  if (raw)
  {
    p->last_touch_ms = now;
    p->up_count = 0U;

    if (!p->touch_down)                                   /* finger down */
    {
      p->touch_down = 1;
      if (!UI_IsVisible())
      {
        UI_SetVisible(1);
        p->ui_dirty = 1;
      }
      else if (ty >= UI_BAR_Y)
      {
        if (tx < UI_BTN_X1)                               /* play / pause */
        {
          p->paused = !p->paused;
          restart_clock(p);
          p->ui_dirty = 1;
        }
        else if ((tx >= (UI_SEEK_X0 - 30U)) && (tx <= (UI_SEEK_X1 + 30U)))
        {
          p->dragging      = 1;
          p->drag_frame    = UI_SeekFrameFromX(tx, p->nframes);
          p->last_scrub_ms = 0U;
          p->ui_dirty      = 1;
        }
      }
      else
      {
        UI_SetVisible(0);
      }
    }
    else if (p->dragging)                                 /* drag */
    {
      uint32_t f = UI_SeekFrameFromX(tx, p->nframes);
      if (f != p->drag_frame) { p->drag_frame = f; p->ui_dirty = 1; }
    }
  }
  else if (p->touch_down)
  {
    if (++p->up_count >= RELEASE_DEBOUNCE)                /* finger up */
    {
      p->touch_down = 0;
      if (p->dragging)
      {
        p->dragging = 0;
        if (p->shown != p->drag_frame)
        {
          if ((rc = show_frame_now(p, p->drag_frame)) != 0) { return rc; }
        }
        restart_clock(p);
        p->ui_dirty = 1;
      }
    }
  }

  if (p->dragging && (p->drag_frame != p->shown) &&
      ((now - p->last_scrub_ms) >= SCRUB_PERIOD_MS))
  {
    p->last_scrub_ms = now;
    if ((rc = show_frame_now(p, p->drag_frame)) != 0) { return rc; }
  }

  if (UI_IsVisible() && !p->paused && !p->dragging &&
      ((now - p->last_touch_ms) > UI_HIDE_MS))
  {
    UI_SetVisible(0);
  }

  if (UI_IsVisible() && (p->ui_dirty || ((now - p->last_ui_ms) >= 250U)))
  {
    UI_Draw(p->paused, p->dragging ? p->drag_frame : p->shown, p->nframes, p->fps);
    p->ui_dirty   = 0;
    p->last_ui_ms = now;
  }
  return 0;
}

/* ---------------- main loop ---------------- */
int Video_Play(const char *path, uint32_t w, uint32_t h, uint32_t fps, uint32_t scale)
{
  static player_t p;
  FRESULT r;
  int     rc;

  memset(&p, 0, sizeof(p));
  p.w = w; p.h = h; p.fps = fps; p.scale = scale;
  p.fb[0]  = FB0_ADDR;
  p.fb[1]  = FB1_ADDR;
  p.back   = 1U;
  p.src[0] = (uint8_t *)s_src0;
  p.src[1] = (uint8_t *)SRC1_ADDR;
  p.frame_bytes = w * h * 2U;
  p.frame_secs  = p.frame_bytes / 512U;

  if ((w == 0U) || (h == 0U) || (fps == 0U) || ((scale != 1U) && (scale != 2U)) ||
      ((w * scale) > LCD_WIDTH) || ((h * scale) > LCD_HEIGHT) || ((w & 1U) != 0U) ||
      ((p.frame_bytes % 512U) != 0U) ||
      ((scale == 2U) && (p.frame_bytes > SRC_MAX_BYTES)))
  {
    return VP_ERR_SIZE;
  }

  r = f_mount(&s_fs, "0:", 1);
  if (r != FR_OK) { printf("f_mount failed: %d\r\n", r); return VP_ERR_MOUNT; }

  r = f_open(&s_fil, path, FA_READ);
  if (r != FR_OK) { printf("f_open(%s) failed: %d\r\n", path, r); return VP_ERR_OPEN; }

  p.nframes = (uint32_t)(f_size(&s_fil) / p.frame_bytes);
  if (p.nframes == 0U) { printf("File smaller than one frame\r\n"); return VP_ERR_EMPTY; }
  if ((f_size(&s_fil) % p.frame_bytes) != 0U)
  {
    printf("WARNING: file size not a multiple of %lu B -> wrong W/H?\r\n",
           (unsigned long)p.frame_bytes);
  }

  p.raw = (build_frag_table(&p) == 0);
  printf("Playing %s: %lux%lu x%lu -> %lux%lu, %lu frames @ %lu fps\r\n", path,
         (unsigned long)w, (unsigned long)h, (unsigned long)scale,
         (unsigned long)(w * scale), (unsigned long)(h * scale),
         (unsigned long)p.nframes, (unsigned long)fps);
  printf("Cluster %lu KB, %lu fragment(s): %s\r\n",
         (unsigned long)(s_fs.csize / 2U), (unsigned long)p.nfrag,
         p.raw ? "raw async IDMA path" : "FatFs path (reformat + copy to defragment)");

  memset((void *)p.fb[0], 0, (size_t)(w * scale) * (h * scale) * 2U);
  memset((void *)p.fb[1], 0, (size_t)(w * scale) * (h * scale) * 2U);
  layer_config(w * scale, h * scale, p.fb[0]);
  UI_Init();

  restart_clock(&p);
  p.stat_t = HAL_GetTick();
  p.shown  = p.nframes - 1U;                 /* so first frame shown is 0 */

  for (;;)
  {
    uint32_t cur, cur_idx;

    if ((rc = handle_input(&p)) != 0) { return rc; }

    if (p.paused || p.dragging)
    {
      commit_request();
      HAL_Delay(5);
      continue;
    }

    /* 1. make sure the frame after the one on screen is (being) fetched */
    if (!p.pf_active)
    {
      p.pf_frame = (p.shown + 1U) % p.nframes;
      p.pf_idx   = 0U;
      if (p.scale == 1U) { wait_flip_done(); }
      if ((rc = read_start(&p, p.pf_frame, read_dst(&p, p.pf_idx))) != 0) { return rc; }
      p.pf_active = 1;
    }

    /* 2. wait for it */
    if ((rc = read_wait(&p)) != 0) { printf("SD read error\r\n"); return rc; }
    cur     = p.pf_frame;
    cur_idx = p.pf_idx;
    p.pf_active = 0;

    /* 3. scale 2: immediately start fetching the following frame into the
          other source buffer, so the SD transfer overlaps upscale + pacing */
    if ((p.scale == 2U) && p.raw)
    {
      p.pf_frame = (cur + 1U) % p.nframes;
      p.pf_idx   = cur_idx ^ 1U;
      if ((rc = read_start(&p, p.pf_frame, read_dst(&p, p.pf_idx))) != 0) { return rc; }
      p.pf_active = 1;
    }

    /* 4. upscale into back buffer now (off the critical display time) */
    if (p.scale == 2U)
    {
      uint32_t t;
      wait_flip_done();
      t = HAL_GetTick();
      upscale2x((const uint32_t *)p.src[cur_idx], p.fb[p.back], p.w, p.h);
      p.up_ms += HAL_GetTick() - t;
    }

    /* 5. wait for display time, polling touch */
    for (;;)
    {
      uint32_t due = p.t0 + (uint32_t)(((uint64_t)p.run * 1000ULL) / p.fps);
      int32_t  lag = (int32_t)(HAL_GetTick() - due);

      if (lag >= 0)
      {
        if (lag > 500) { restart_clock(&p); }
        break;
      }
      if ((rc = handle_input(&p)) != 0) { return rc; }
      if (p.paused || p.dragging) { break; }
      HAL_Delay(1);
    }
    if (p.dragging) { continue; }            /* scrub already replaced the frame */

    /* 6. flip at next VBlank (non-blocking) */
    flip_request(p.fb[p.back]);
    p.back ^= 1U;
    p.shown = cur;
    p.run++;

    /* stats */
    p.stat_n++;
    if ((HAL_GetTick() - p.stat_t) >= 1000U)
    {
      uint32_t n = (p.stat_n != 0U) ? p.stat_n : 1U;
      printf("fps %lu | SD wait %lu ms | upscale %lu ms\r\n", (unsigned long)p.stat_n,
             (unsigned long)(p.sd_wait_ms / n), (unsigned long)(p.up_ms / n));
      p.stat_n = 0U;
      p.sd_wait_ms = 0U;
      p.up_ms  = 0U;
      p.stat_t = HAL_GetTick();
      HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);
    }
  }
}
