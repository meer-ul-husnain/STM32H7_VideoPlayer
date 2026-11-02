# STM32H743 SD-card video player (raw RGB565 -> 7" 1024x600 RGB LCD)

Build: open in STM32CubeIDE -> Project > Build (Debug or Release). No extra settings.

## What was added
| File | Purpose |
|---|---|
| Core/Src/main.c | 400 MHz clocks, MPU (SDRAM region), GPIO, USART1, FMC SDRAM, LTDC 1024x600, SDMMC1 init, app |
| Core/Src/stm32h7xx_hal_msp.c | Pin mux: LTDC (PI12-15, PJ0-15, PK0-7), FMC SDRAM, SDMMC1, USART1 |
| Core/Src/stm32h7xx_it.c | LTDC_IRQHandler, SDMMC1_IRQHandler |
| Core/Inc/stm32h7xx_hal_conf.h | LTDC, SDRAM, SD, UART modules enabled |
| Core/Src/sdram.c, Core/Inc/sdram.h | IS42S32800J JEDEC init + test |
| Core/Src/video_player.c/.h | FatFs read -> SDRAM double buffer -> LTDC flip at VBlank |
| Core/Src/touch.c/.h | FT5x06 / GT911 touch, soft I2C PB10/PB11, RST PB12, INT PB5 |
| Core/Src/ui.c/.h | Control bar on LTDC layer 2 (ARGB4444) |
| Core/Src/sd_diskio.c | FatFs disk I/O on SDMMC1 IDMA (read-only) |
| Core/Src/FatFs/ff.c, ffunicode.c; Core/Inc/ff.h, ffconf.h, diskio.h | FatFs R0.15 (LFN + exFAT, read-only) |
| Drivers/STM32H7xx_HAL_Driver | + ltdc, sdram, ll_fmc, sd, sd_ex, ll_sdmmc, ll_delayblock, uart, uart_ex (HAL v1.11.5) |

## SD card
Root file `VIDEO.BIN` = raw RGB565LE, 512x300, 30 fps, shown pixel-doubled full screen
(matches VIDEO_W/H/FPS/SCALE in main.c).

    ffmpeg -i in.mp4 -an -vf "scale=512:300:force_original_aspect_ratio=increase:flags=lanczos,crop=512:300,fps=30" -pix_fmt rgb565le -f rawvideo VIDEO.BIN

## Touch controls
Tap video: show/hide bar. Bar left button: play/pause. Tap/drag seek bar: seek on release.
Dragging shows a live preview (~16 updates/s). Bar auto-hides after 3 s while playing.
Touch: FT5x06 or GT911 auto-detected (touch.c); per-chip orientation macros in touch.h
(FT5x06 default: X/Y swapped for landscape). Set TOUCH_DEBUG 1 to print coordinates.
Console prints every second: fps | SD wait ms | upscale ms.
Frames are read as raw LBA ranges with async IDMA (FatFs fast-seek cluster map),
overlapped with upscaling. Keep the file unfragmented: format, then copy VIDEO.BIN first.
I+D cache enabled; SDRAM frame buffers non-cacheable (MPU region 1), video src[1]
window 0xD0600000 cacheable write-through (MPU region 2); IDMA buffers invalidated
in sd_diskio.c. Hot loops forced -O3 so the Debug build is also fast.

## Do NOT regenerate code from STM32H7.ioc
The .ioc still describes the empty project. "Generate Code" would overwrite
SystemClock_Config, MX_* functions, MSP and hal_conf.h. Edit the code directly.

## Console (COM port, 115200 8N1)
Prints clocks, SDRAM result, SD speed, file info and measured fps.
LED: green toggles 1 Hz while playing; blue blinking = error.
