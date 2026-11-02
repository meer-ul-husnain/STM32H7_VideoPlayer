# STM32H7 Video Player – 7-Inch RGB LCD

A standalone video player built using the **STM32H743 microcontroller** and a **7-inch Alientek IPS capacitive-touch display**.

The project plays raw RGB565 video directly from a MicroSD card and displays it on a 1024 × 600 RGB LCD. It uses external SDRAM, LTDC, SDMMC, FatFs, and double buffering to achieve smooth video playback at **30 FPS**.

No PC, Raspberry Pi, or external GPU is required during playback. Just an STM32, an SD card, and a display.

## Project Highlights

- STM32H743-based standalone video player
- 7-inch Alientek IPS capacitive-touch display
- 1024 × 600 RGB display resolution
- Raw RGB565 video playback from a MicroSD card
- Smooth 30 FPS playback
- External SDRAM for frame buffering
- LTDC double-buffered display output
- Touch-based play/pause and video seeking
- On-screen playback controls
- No external GPU required

## Hardware Requirements

| Component | Description |
|---|---|
| Microcontroller | STM32H743 |
| LCD display | 7.0-inch Alientek IPS LCD |
| Display resolution | 1024 × 600 |
| Display interface | RGB |
| LCD driver board | RGB driver board supplied with the display |
| Touch panel | Capacitive touch |
| External SDRAM | IS42S32800J |
| Storage | MicroSD card |
| Programmer/debugger | ST-Link |
| Serial debugging | USB-to-UART adapter (if required by your board) |

### 1. LCD Display

The project uses a **7.0-inch Alientek IPS capacitive-touch LCD with an RGB driver board**, supplied by Ronbo Electronics.

**Display specifications:**
- Screen size: 7.0 inches
- Resolution: 1024 × 600 pixels
- Panel type: IPS
- Display interface: RGB
- Touch interface: Capacitive
- Driver board: Included

**Display product reference:**

[7.0-Inch Alientek IPS 1024×600 LCD – Ronbo Electronics](https://ronboelectronics.en.made-in-china.com/product/UwBJeTZAbYVl/China-7-0-Inch-IPS-1024X600-Alientek-Stm32-Capacitive-Touch-LCD-Display-with-RGB-Driver-Board.html)

### 2. Controller Board

The controller hardware used in this project is referenced below.

[Controller Board – AliExpress](https://www.aliexpress.com/item/1005009763250594.html)

### 3. External SDRAM

The project uses an IS42S32800J external SDRAM to provide the memory required for video frame buffering and display operations.

## Software Requirements

- STM32CubeIDE
- STM32 HAL libraries
- FFmpeg
- FatFs
- Git

## Project Architecture

The video player follows this processing pipeline:

1. Read raw video data from the MicroSD card using SDMMC1.
2. Use FatFs to access the video file.
3. Transfer video data into SDRAM.
4. Process and upscale the video frames.
5. Use LTDC to display frames on the RGB LCD.
6. Switch between frame buffers to provide smooth playback.

### Main Peripherals

| Peripheral | Function |
|---|---|
| LTDC | RGB LCD display output |
| FMC | External SDRAM interface |
| SDMMC1 | MicroSD card communication |
| USART1 | Debugging and performance monitoring |
| GPIO | Hardware control |
| I2C / Software I2C | Touch controller communication |

## Video File Requirements

The firmware does not decode MP4 files directly.

Instead, videos must be converted into raw RGB565 little-endian format before being copied to the SD card.

**Video format:**

| Parameter | Value |
|---|---|
| File name | `VIDEO.BIN` |
| Format | Raw RGB565LE |
| Video width | 512 pixels |
| Video height | 300 pixels |
| Frame rate | 30 FPS |
| Audio | Not supported |
| Display resolution | 1024 × 600 |

The firmware scales each 512 × 300 frame to fill the 1024 × 600 display.

## Convert MP4 to VIDEO.BIN

Install FFmpeg on your computer.

Run the following command:

```bash
ffmpeg -i input.mp4 -an -vf "scale=512:300:force_original_aspect_ratio=increase:flags=lanczos,crop=512:300,fps=30" -pix_fmt rgb565le -f rawvideo VIDEO.BIN
```

This command:

- Removes audio.
- Resizes the video to 512 × 300.
- Maintains the selected aspect ratio by scaling and cropping.
- Converts the frame rate to 30 FPS.
- Converts the pixel format to RGB565 little-endian.
- Generates the raw video file `VIDEO.BIN`.

**Important:** The output filename must be exactly `VIDEO.BIN`.

## Prepare the MicroSD Card

1. Format your MicroSD card. FAT32 is recommended.
2. Copy `VIDEO.BIN` into the root directory.
3. Do not place the file inside another folder.
4. Keep the video file unfragmented. Ideally, format the card and copy `VIDEO.BIN` as the first file.

Your SD card should look like this:

```text
MicroSD Card/
└── VIDEO.BIN
```

The firmware's FatFs configuration also supports exFAT.

## Build and Flash the Firmware

### Step 1: Clone the Repository

```bash
git clone https://github.com/meer-ul-husnain/STM32H7_PlayVideo.git
```

### Step 2: Open the Project

1. Open STM32CubeIDE.
2. Import the project into your workspace.
3. Select the STM32H743 target configuration.
4. Build the project.

**Important:** Do not regenerate the project code using the STM32H7 `.ioc` file. The IOC currently describes an empty project, and code generation may overwrite custom initialization functions, clock configuration, MSP functions, and HAL configuration.

### Step 3: Flash the MCU

1. Connect the ST-Link debugger to the STM32H743 board using SWD.
2. Connect the board to your computer.
3. Open the project in STM32CubeIDE.
4. Build the firmware.
5. Select **Run → Debug** or **Run → Run** to program the MCU.

### Step 4: Start Playback

1. Insert the MicroSD card containing `VIDEO.BIN`.
2. Connect the Alientek LCD display.
3. Power on the STM32H743 board.
4. The firmware initializes the peripherals and loads the video.
5. The video starts playing on the LCD.

## Touch Controls

The project supports capacitive touch through FT5x06 and GT911 touch controllers.

| Touch Action | Function |
|---|---|
| Tap the video | Show or hide the playback control bar |
| Tap the play/pause button | Play or pause video |
| Tap the progress bar | Seek to a video position |
| Drag the progress bar | Preview and seek through the video |

The playback control bar automatically hides after approximately 3 seconds while the video is playing.

Touch orientation settings can be adjusted using the configuration macros in `touch.h`.

Set `TOUCH_DEBUG` to `1` to print touch coordinates for debugging.

## Performance and Memory Optimization

Several optimizations are used to improve playback performance:

- External SDRAM is used for frame buffering.
- LTDC double buffering reduces display tearing.
- SDMMC1 IDMA supports asynchronous data transfers.
- Video reading overlaps with frame upscaling.
- Fast-seek cluster mapping improves file access.
- Cache configuration is optimized for the SDRAM regions.
- Frame buffers are configured using MPU memory attributes.
- Hot processing loops are optimized using `-O3`.

The firmware also monitors SD-card read time and frame-processing time to help identify performance bottlenecks.

## Debugging and Performance Monitoring

Connect a USB-to-UART adapter to the USART1 debug interface if serial output is not already available through your board.

**Serial configuration:**

```text
Baud rate: 115200
Data bits: 8
Stop bits: 1
Parity: None
```

The firmware reports information such as:

- System clock configuration
- SDRAM initialization status
- SD-card read speed
- Video file information
- Frames per second (FPS)
- SD-card waiting time
- Frame upscaling time

The green LED toggles at approximately 1 Hz during playback. A blinking blue LED indicates an error.

## Troubleshooting

| Problem | Possible Solution |
|---|---|
| SD card not detected | Check SDMMC connections and card formatting |
| VIDEO.BIN not found | Confirm the exact filename and root directory |
| Display remains blank | Check RGB connections, LTDC configuration, and display power |
| Touch not working | Check touch I2C connections and controller configuration |
| Low FPS | Check SD-card speed and file fragmentation |
| SDRAM initialization fails | Check FMC configuration and SDRAM hardware connections |
| Serial output unavailable | Verify USART1 connections and 115200 baud configuration |

## Important Notes

- This project plays raw RGB565 video, not MP4 files directly.
- Video conversion must be performed on a PC before playback.
- Audio playback is not implemented.
- The video file must be named `VIDEO.BIN`.
- The firmware uses custom peripheral initialization.
- Avoid regenerating code from the existing `.ioc` file.

## Repository

**GitHub:**  
https://github.com/meer-ul-husnain/STM32H7_PlayVideo

**Instagram:**  
[@embeddedwithmeer](https://www.instagram.com/embeddedwithmeer/)

---

## Built With

- STM32H743
- Alientek 7-inch IPS RGB LCD
- External SDRAM
- SDMMC and FatFs
- LTDC
- Embedded C

**Built with STM32. No GPU. Just embedded engineering.** 🚀
