/*
 * SSD1306 OLED Driver for ESP-IDF (128×64, I2C)
 * – framebuffer-based with 5×7 font & 7-segment rendering
 */
#include "ssd1306.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define TAG             "SSD1306"
#define SSD1306_ADDR    0x3C
#define I2C_PORT        I2C_NUM_0
#define FB_SIZE         (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

/* ── Framebuffer ───────────────────────────────────────────── */
static uint8_t framebuffer[FB_SIZE];

/* ── 5×7 Font (ASCII 32-126, 5 bytes/glyph, column-major) ── */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x41,0x51,0x32}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x08,0x14,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x10,0x08,0x08,0x10,0x08}, /* ~ */
};

/* ── I2C Low-Level ─────────────────────────────────────────── */
static esp_err_t ssd1306_cmd(uint8_t c)
{
    i2c_cmd_handle_t lnk = i2c_cmd_link_create();
    i2c_master_start(lnk);
    i2c_master_write_byte(lnk, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(lnk, 0x00, true);   /* Co=0, D/C#=0  →  command */
    i2c_master_write_byte(lnk, c, true);
    i2c_master_stop(lnk);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, lnk, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(lnk);
    return r;
}

static esp_err_t ssd1306_cmds(const uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t lnk = i2c_cmd_link_create();
    i2c_master_start(lnk);
    i2c_master_write_byte(lnk, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(lnk, 0x00, true);
    i2c_master_write(lnk, buf, len, true);
    i2c_master_stop(lnk);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, lnk, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(lnk);
    return r;
}

static esp_err_t ssd1306_data(const uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t lnk = i2c_cmd_link_create();
    i2c_master_start(lnk);
    i2c_master_write_byte(lnk, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(lnk, 0x40, true);   /* Co=0, D/C#=1  →  data */
    i2c_master_write(lnk, buf, len, true);
    i2c_master_stop(lnk);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, lnk, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(lnk);
    return r;
}

/* ── Init ──────────────────────────────────────────────────── */
esp_err_t ssd1306_init(int sda_pin, int scl_pin)
{
    /* I2C master */
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = sda_pin,
        .scl_io_num       = scl_pin,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
    vTaskDelay(pdMS_TO_TICKS(100));

    /* SSD1306 init sequence */
    static const uint8_t init[] = {
        0xAE,             /* display OFF                   */
        0xD5, 0x80,       /* clock divider (default)       */
        0xA8, 0x3F,       /* multiplex 64                  */
        0xD3, 0x00,       /* display offset 0              */
        0x40,             /* start line 0                  */
        0x8D, 0x14,       /* charge pump ON                */
        0x20, 0x00,       /* horizontal addressing mode    */
        0xA0,             /* segment remap (normal)        */
        0xC0,             /* COM scan normal (rotated 180) */
        0xDA, 0x12,       /* COM pin config                */
        0x81, 0xCF,       /* contrast                      */
        0xD9, 0xF1,       /* pre-charge                    */
        0xDB, 0x40,       /* VCOMH deselect                */
        0xA4,             /* output follows RAM             */
        0xA6,             /* normal (not inverted)          */
        0xAF,             /* display ON                    */
    };
    for (size_t i = 0; i < sizeof(init); i++) {
        esp_err_t r = ssd1306_cmd(init[i]);
        if (r != ESP_OK) { ESP_LOGE(TAG, "init[%d] fail", (int)i); return r; }
    }

    ssd1306_clear();
    ssd1306_update();
    ESP_LOGI(TAG, "SSD1306 ready (128x64)");
    return ESP_OK;
}

/* ── Display Control ───────────────────────────────────────── */
void ssd1306_update(void)
{
    uint8_t a[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    ssd1306_cmds(a, sizeof(a));
    ssd1306_data(framebuffer, FB_SIZE);
}

void ssd1306_clear(void)  { memset(framebuffer, 0x00, FB_SIZE); }
void ssd1306_fill(uint8_t c) { memset(framebuffer, c ? 0xFF : 0x00, FB_SIZE); }

void ssd1306_set_contrast(uint8_t v) { ssd1306_cmd(0x81); ssd1306_cmd(v); }
void ssd1306_display_on(bool on)     { ssd1306_cmd(on ? 0xAF : 0xAE); }
void ssd1306_invert(bool inv)        { ssd1306_cmd(inv ? 0xA7 : 0xA6); }

/* ── Pixel ─────────────────────────────────────────────────── */
void ssd1306_draw_pixel(int16_t x, int16_t y, uint8_t color)
{
    if ((uint16_t)x >= SSD1306_WIDTH || (uint16_t)y >= SSD1306_HEIGHT) return;
    if (color)
        framebuffer[x + (y >> 3) * SSD1306_WIDTH] |=  (1 << (y & 7));
    else
        framebuffer[x + (y >> 3) * SSD1306_WIDTH] &= ~(1 << (y & 7));
}

/* ── Lines ─────────────────────────────────────────────────── */
void ssd1306_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color)
{
    for (int16_t i = 0; i < w; i++) ssd1306_draw_pixel(x + i, y, color);
}

void ssd1306_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color)
{
    for (int16_t i = 0; i < h; i++) ssd1306_draw_pixel(x, y + i, color);
}

/* ── Rectangles ────────────────────────────────────────────── */
void ssd1306_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
    ssd1306_draw_hline(x, y, w, c);
    ssd1306_draw_hline(x, y + h - 1, w, c);
    ssd1306_draw_vline(x, y, h, c);
    ssd1306_draw_vline(x + w - 1, y, h, c);
}

void ssd1306_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
    for (int16_t j = 0; j < h; j++)
        for (int16_t i = 0; i < w; i++)
            ssd1306_draw_pixel(x + i, y + j, c);
}

/* ── Circles ───────────────────────────────────────────────── */
void ssd1306_draw_circle(int16_t cx, int16_t cy, int16_t r, uint8_t c)
{
    int16_t x = r, y = 0, d = 1 - r;
    while (x >= y) {
        ssd1306_draw_pixel(cx+x, cy+y, c);  ssd1306_draw_pixel(cx-x, cy+y, c);
        ssd1306_draw_pixel(cx+x, cy-y, c);  ssd1306_draw_pixel(cx-x, cy-y, c);
        ssd1306_draw_pixel(cx+y, cy+x, c);  ssd1306_draw_pixel(cx-y, cy+x, c);
        ssd1306_draw_pixel(cx+y, cy-x, c);  ssd1306_draw_pixel(cx-y, cy-x, c);
        y++;
        if (d <= 0) { d += 2*y + 1; }
        else        { x--; d += 2*(y - x) + 1; }
    }
}

void ssd1306_fill_circle(int16_t cx, int16_t cy, int16_t r, uint8_t c)
{
    for (int16_t y = -r; y <= r; y++)
        for (int16_t x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                ssd1306_draw_pixel(cx+x, cy+y, c);
}

void ssd1306_draw_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                                int16_t r, uint8_t c)
{
    ssd1306_draw_hline(x+r, y,       w-2*r, c);      /* top    */
    ssd1306_draw_hline(x+r, y+h-1,   w-2*r, c);      /* bottom */
    ssd1306_draw_vline(x,     y+r,    h-2*r, c);      /* left   */
    ssd1306_draw_vline(x+w-1, y+r,    h-2*r, c);      /* right  */
    /* quarter arcs */
    int16_t ax = r, ay = 0, d = 1 - r;
    while (ax >= ay) {
        ssd1306_draw_pixel(x+w-1-r+ax, y+r-ay,     c);
        ssd1306_draw_pixel(x+w-1-r+ay, y+r-ax,     c);
        ssd1306_draw_pixel(x+r-ax,     y+r-ay,     c);
        ssd1306_draw_pixel(x+r-ay,     y+r-ax,     c);
        ssd1306_draw_pixel(x+w-1-r+ax, y+h-1-r+ay, c);
        ssd1306_draw_pixel(x+w-1-r+ay, y+h-1-r+ax, c);
        ssd1306_draw_pixel(x+r-ax,     y+h-1-r+ay, c);
        ssd1306_draw_pixel(x+r-ay,     y+h-1-r+ax, c);
        ay++;
        if (d <= 0) { d += 2*ay + 1; }
        else        { ax--; d += 2*(ay - ax) + 1; }
    }
}

/* ── Text ──────────────────────────────────────────────────── */
void ssd1306_draw_char(int16_t x, int16_t y, char ch, uint8_t color, uint8_t sz)
{
    if (ch < 32 || ch > 126) return;
    const uint8_t *g = font5x7[(int)ch - 32];
    for (int8_t col = 0; col < 5; col++) {
        uint8_t line = g[col];
        for (int8_t row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                if (sz <= 1)
                    ssd1306_draw_pixel(x + col, y + row, color);
                else
                    ssd1306_fill_rect(x + col*sz, y + row*sz, sz, sz, color);
            }
        }
    }
}

void ssd1306_draw_string(int16_t x, int16_t y, const char *s,
                          uint8_t color, uint8_t sz)
{
    while (*s) {
        ssd1306_draw_char(x, y, *s++, color, sz);
        x += 6 * sz;
    }
}

int16_t ssd1306_string_width(const char *s, uint8_t sz)
{
    int n = 0;
    while (s[n]) n++;
    return n * 6 * sz;
}

void ssd1306_draw_string_centered(int16_t y, const char *s,
                                   uint8_t color, uint8_t sz)
{
    int16_t w = ssd1306_string_width(s, sz);
    ssd1306_draw_string((SSD1306_WIDTH - w) / 2, y, s, color, sz);
}

/* ── 7-Segment Digits ──────────────────────────────────────── */
/*
 *     _a_
 *    |   |
 *    f   b
 *    |_g_|
 *    |   |
 *    e   c
 *    |_d_|
 *
 * bit order:  gfedcba
 */
static const uint8_t seg7_map[10] = {
    0x3F, /* 0  segments abcdef  */
    0x06, /* 1  segments bc      */
    0x5B, /* 2  segments abdeg   */
    0x4F, /* 3  segments abcdg   */
    0x66, /* 4  segments bcfg    */
    0x6D, /* 5  segments acdfg   */
    0x7D, /* 6  segments acdefg  */
    0x07, /* 7  segments abc     */
    0x7F, /* 8  segments abcdefg */
    0x6F, /* 9  segments abcdfg  */
};

void ssd1306_draw_7seg_digit(int16_t x, int16_t y, uint8_t digit,
                              int16_t w, int16_t h, int16_t t,
                              uint8_t color)
{
    if (digit > 9) return;
    uint8_t s = seg7_map[digit];
    int16_t mid = h / 2;
    int16_t ht  = t / 2;          /* half-thickness for centring g */

    /* a – top horizontal */
    if (s & 0x01) ssd1306_fill_rect(x + t, y, w - 2*t, t, color);
    /* b – top-right vertical */
    if (s & 0x02) ssd1306_fill_rect(x + w - t, y + t, t, mid - t, color);
    /* c – bottom-right vertical */
    if (s & 0x04) ssd1306_fill_rect(x + w - t, y + mid + ht, t, mid - t - ht, color);
    /* d – bottom horizontal */
    if (s & 0x08) ssd1306_fill_rect(x + t, y + h - t, w - 2*t, t, color);
    /* e – bottom-left vertical */
    if (s & 0x10) ssd1306_fill_rect(x, y + mid + ht, t, mid - t - ht, color);
    /* f – top-left vertical */
    if (s & 0x20) ssd1306_fill_rect(x, y + t, t, mid - t, color);
    /* g – middle horizontal */
    if (s & 0x40) ssd1306_fill_rect(x + t, y + mid - ht, w - 2*t, t, color);
}

void ssd1306_draw_7seg_colon(int16_t x, int16_t y, int16_t h,
                              int16_t ds, uint8_t color)
{
    int16_t cx = x + ds / 2;
    ssd1306_fill_rect(cx, y + h/3 - ds/2,     ds, ds, color);
    ssd1306_fill_rect(cx, y + 2*h/3 - ds/2,   ds, ds, color);
}

/* ── Bitmap ────────────────────────────────────────────────── */
void ssd1306_draw_bitmap(int16_t x, int16_t y, const uint8_t *bmp,
                          int16_t w, int16_t h, uint8_t color)
{
    int16_t byte_w = (w + 7) / 8;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            uint8_t b = bmp[j * byte_w + i / 8];
            if (b & (0x80 >> (i & 7)))
                ssd1306_draw_pixel(x + i, y + j, color);
        }
    }
}
