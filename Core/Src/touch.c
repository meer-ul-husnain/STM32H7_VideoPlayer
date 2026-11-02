/**
  * @file  touch.c
  * @brief Capacitive touch driver (FT5x06 / GT911 auto-detect) over bit-banged
  *        I2C on PB10/PB11 at ~200 kHz. Polled, no interrupt required.
  */
#include "touch.h"
#include <stdio.h>

#define TP_PORT       GPIOB
#define TP_SCL        GPIO_PIN_10
#define TP_SDA        GPIO_PIN_11
#define TP_RST        GPIO_PIN_12
#define TP_INT        GPIO_PIN_5

#define FT_ADDR       0x38U
#define GT_ADDR_A     0x5DU
#define GT_ADDR_B     0x14U

#define FT_REG_MODE   0x00U
#define FT_REG_STATUS 0x02U
#define FT_REG_GMODE  0xA4U
#define FT_REG_CHIPID 0xA3U

#define GT_REG_PID    0x8140U
#define GT_REG_STATUS 0x814EU
#define GT_REG_P1     0x8150U

static touch_chip_t s_chip = TOUCH_NONE;
static uint8_t      s_addr;
static uint8_t      s_gt_pressed;
static uint16_t     s_gt_x, s_gt_y;
static uint32_t     s_half_period_cycles;

/* ---------------- microsecond timing via DWT cycle counter ---------------- */
static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55U;                 /* unlock on Cortex-M7 */
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_half_period_cycles = SystemCoreClock / 400000U;   /* 2.5 us -> ~200 kHz */
}

static void i2c_delay(void)
{
  uint32_t t0 = DWT->CYCCNT;
  while ((DWT->CYCCNT - t0) < s_half_period_cycles) { }
}

static void delay_us(uint32_t us)
{
  uint32_t t0 = DWT->CYCCNT;
  uint32_t n  = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - t0) < n) { }
}

/* ---------------- bit-banged I2C primitives ---------------- */
#define SCL_H()   (TP_PORT->BSRR = TP_SCL)
#define SCL_L()   (TP_PORT->BSRR = (uint32_t)TP_SCL << 16)
#define SDA_H()   (TP_PORT->BSRR = TP_SDA)
#define SDA_L()   (TP_PORT->BSRR = (uint32_t)TP_SDA << 16)
#define SDA_IN()  ((TP_PORT->IDR & TP_SDA) != 0U)
#define SCL_IN()  ((TP_PORT->IDR & TP_SCL) != 0U)

static void scl_release(void)
{
  uint32_t t0 = DWT->CYCCNT;

  SCL_H();
  /* honour clock stretching, max ~200 us */
  while (!SCL_IN() && ((DWT->CYCCNT - t0) < (SystemCoreClock / 5000U))) { }
}

static void i2c_start(void)
{
  SDA_H(); scl_release(); i2c_delay();
  SDA_L(); i2c_delay();
  SCL_L(); i2c_delay();
}

static void i2c_stop(void)
{
  SDA_L(); i2c_delay();
  scl_release(); i2c_delay();
  SDA_H(); i2c_delay();
}

/* returns 1 if ACK received */
static int i2c_write_byte(uint8_t b)
{
  int ack;

  for (int i = 0; i < 8; i++)
  {
    if ((b & 0x80U) != 0U) { SDA_H(); } else { SDA_L(); }
    b <<= 1;
    i2c_delay();
    scl_release(); i2c_delay();
    SCL_L();
  }
  SDA_H(); i2c_delay();
  scl_release(); i2c_delay();
  ack = SDA_IN() ? 0 : 1;
  SCL_L(); i2c_delay();
  return ack;
}

static uint8_t i2c_read_byte(int ack)
{
  uint8_t b = 0U;

  SDA_H();
  for (int i = 0; i < 8; i++)
  {
    i2c_delay();
    scl_release(); i2c_delay();
    b = (uint8_t)((b << 1) | (SDA_IN() ? 1U : 0U));
    SCL_L();
  }
  if (ack) { SDA_L(); } else { SDA_H(); }
  i2c_delay();
  scl_release(); i2c_delay();
  SCL_L();
  SDA_H(); i2c_delay();
  return b;
}

static int i2c_probe(uint8_t addr)
{
  int ack;

  i2c_start();
  ack = i2c_write_byte((uint8_t)(addr << 1));
  i2c_stop();
  return ack;
}

/* reg_len = 1 (FT) or 2 (GT, big-endian register address) */
static int i2c_read_regs(uint8_t addr, uint16_t reg, int reg_len, uint8_t *buf, uint32_t n)
{
  i2c_start();
  if (!i2c_write_byte((uint8_t)(addr << 1))) { i2c_stop(); return -1; }
  if (reg_len == 2)
  {
    if (!i2c_write_byte((uint8_t)(reg >> 8))) { i2c_stop(); return -1; }
  }
  if (!i2c_write_byte((uint8_t)reg)) { i2c_stop(); return -1; }

  i2c_start();                                    /* repeated start */
  if (!i2c_write_byte((uint8_t)((addr << 1) | 1U))) { i2c_stop(); return -1; }
  for (uint32_t i = 0; i < n; i++)
  {
    buf[i] = i2c_read_byte(i < (n - 1U));
  }
  i2c_stop();
  return 0;
}

static int i2c_write_reg(uint8_t addr, uint16_t reg, int reg_len, uint8_t val)
{
  int ok = 1;

  i2c_start();
  ok &= i2c_write_byte((uint8_t)(addr << 1));
  if (reg_len == 2) { ok &= i2c_write_byte((uint8_t)(reg >> 8)); }
  ok &= i2c_write_byte((uint8_t)reg);
  ok &= i2c_write_byte(val);
  i2c_stop();
  return ok ? 0 : -1;
}

/* ---------------- public API ---------------- */
touch_chip_t Touch_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  dwt_init();

  /* SCL/SDA: open-drain, idle high */
  TP_PORT->BSRR = TP_SCL | TP_SDA;
  g.Pin   = TP_SCL | TP_SDA;
  g.Mode  = GPIO_MODE_OUTPUT_OD;
  g.Pull  = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TP_PORT, &g);

  /* RST and INT as push-pull outputs for the reset sequence.
     INT low while RST rises selects GT911 address 0x5D. */
  TP_PORT->BSRR = ((uint32_t)TP_RST << 16) | ((uint32_t)TP_INT << 16);
  g.Pin  = TP_RST | TP_INT;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TP_PORT, &g);

  HAL_Delay(20);
  TP_PORT->BSRR = TP_RST;                  /* release reset */
  delay_us(100);
  HAL_Delay(60);

  /* INT back to floating input */
  g.Pin  = TP_INT;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TP_PORT, &g);

  HAL_Delay(200);                          /* FT5x06 needs ~200 ms after reset */

  s_chip = TOUCH_NONE;
  if (i2c_probe(FT_ADDR))
  {
    uint8_t id = 0U;
    s_addr = FT_ADDR;
    s_chip = TOUCH_FT5X06;
    (void)i2c_write_reg(FT_ADDR, FT_REG_MODE, 1, 0x00U);   /* normal operating mode */
    (void)i2c_write_reg(FT_ADDR, FT_REG_GMODE, 1, 0x00U);  /* polling mode */
    (void)i2c_read_regs(FT_ADDR, FT_REG_CHIPID, 1, &id, 1U);
    printf("Touch: FocalTech FT5x06 family, chip id 0x%02X\r\n", id);
  }
  else if (i2c_probe(GT_ADDR_A) || i2c_probe(GT_ADDR_B))
  {
    uint8_t pid[5] = {0};
    s_addr = i2c_probe(GT_ADDR_A) ? GT_ADDR_A : GT_ADDR_B;
    s_chip = TOUCH_GT911;
    (void)i2c_read_regs(s_addr, GT_REG_PID, 2, pid, 4U);
    pid[4] = 0U;
    (void)i2c_write_reg(s_addr, GT_REG_STATUS, 2, 0x00U);
    printf("Touch: Goodix GT%s at 0x%02X\r\n", (char *)pid, s_addr);
  }
  else
  {
    printf("Touch: no controller found (check LCD FPC / pins 35-39)\r\n");
  }
  return s_chip;
}

static void map_coords(uint16_t rx, uint16_t ry, uint16_t *x, uint16_t *y)
{
  uint32_t px = rx, py = ry;
  int swap, inv_x, inv_y;

  if (s_chip == TOUCH_FT5X06)
  {
    swap = TOUCH_FT_SWAP_XY; inv_x = TOUCH_FT_INVERT_X; inv_y = TOUCH_FT_INVERT_Y;
  }
  else
  {
    swap = TOUCH_GT_SWAP_XY; inv_x = TOUCH_GT_INVERT_X; inv_y = TOUCH_GT_INVERT_Y;
  }

  if (swap)
  {
    px = ry;
    py = rx;
  }
  if (px >= TOUCH_RAW_MAX_X) { px = TOUCH_RAW_MAX_X - 1U; }
  if (py >= TOUCH_RAW_MAX_Y) { py = TOUCH_RAW_MAX_Y - 1U; }
  if (inv_x) { px = TOUCH_RAW_MAX_X - 1U - px; }
  if (inv_y) { py = TOUCH_RAW_MAX_Y - 1U - py; }

  /* scale raw range to 1024 x 600 */
  *x = (uint16_t)((px * 1024U) / TOUCH_RAW_MAX_X);
  *y = (uint16_t)((py * 600U) / TOUCH_RAW_MAX_Y);
}

int Touch_Read(uint16_t *x, uint16_t *y)
{
  uint8_t  b[5];
  uint16_t rx = 0U, ry = 0U;
  int      pressed = 0;

  if (s_chip == TOUCH_FT5X06)
  {
    if (i2c_read_regs(s_addr, FT_REG_STATUS, 1, b, 5U) != 0) { return 0; }
    uint8_t n     = b[0] & 0x0FU;
    uint8_t event = b[1] >> 6;                     /* 0 down, 1 up, 2 contact */
    if ((n >= 1U) && (n <= 5U) && (event != 1U))
    {
      rx = (uint16_t)(((uint16_t)(b[1] & 0x0FU) << 8) | b[2]);
      ry = (uint16_t)(((uint16_t)(b[3] & 0x0FU) << 8) | b[4]);
      pressed = 1;
    }
  }
  else if (s_chip == TOUCH_GT911)
  {
    uint8_t st;
    if (i2c_read_regs(s_addr, GT_REG_STATUS, 2, &st, 1U) != 0) { return 0; }
    if ((st & 0x80U) != 0U)                        /* new data ready */
    {
      s_gt_pressed = 0U;
      if (((st & 0x0FU) != 0U) && (i2c_read_regs(s_addr, GT_REG_P1, 2, b, 4U) == 0))
      {
        s_gt_x = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
        s_gt_y = (uint16_t)(b[2] | ((uint16_t)b[3] << 8));
        s_gt_pressed = 1U;
      }
      (void)i2c_write_reg(s_addr, GT_REG_STATUS, 2, 0x00U);
    }
    rx = s_gt_x;
    ry = s_gt_y;
    pressed = (int)s_gt_pressed;
  }
  else
  {
    return 0;
  }

  if (pressed)
  {
    map_coords(rx, ry, x, y);
#if TOUCH_DEBUG
    printf("touch raw %u,%u -> %u,%u\r\n", rx, ry, *x, *y);
#endif
  }
  return pressed;
}
