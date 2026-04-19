#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

#define SSD1306_BLACK   0
#define SSD1306_WHITE   1

/* ── Lifecycle ─────────────────────────────────────────────── */
esp_err_t ssd1306_init(int sda_pin, int scl_pin);
void      ssd1306_update(void);
void      ssd1306_clear(void);
void      ssd1306_fill(uint8_t color);
void      ssd1306_set_contrast(uint8_t value);
void      ssd1306_display_on(bool on);
void      ssd1306_invert(bool inv);

/* ── Drawing Primitives ────────────────────────────────────── */
void ssd1306_draw_pixel(int16_t x, int16_t y, uint8_t color);
void ssd1306_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color);
void ssd1306_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color);
void ssd1306_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssd1306_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssd1306_draw_circle(int16_t cx, int16_t cy, int16_t r, uint8_t color);
void ssd1306_fill_circle(int16_t cx, int16_t cy, int16_t r, uint8_t color);
void ssd1306_draw_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                                int16_t r, uint8_t color);

/* ── Text (built-in 5×7 font in 6×8 cell) ─────────────────── */
/*  size=1 → 6×8,  size=2 → 12×16,  etc.                      */
void    ssd1306_draw_char(int16_t x, int16_t y, char c,
                          uint8_t color, uint8_t size);
void    ssd1306_draw_string(int16_t x, int16_t y, const char *s,
                            uint8_t color, uint8_t size);
int16_t ssd1306_string_width(const char *s, uint8_t size);
void    ssd1306_draw_string_centered(int16_t y, const char *s,
                                     uint8_t color, uint8_t size);

/* ── 7-Segment Style Digits ────────────────────────────────── */
void ssd1306_draw_7seg_digit(int16_t x, int16_t y, uint8_t digit,
                              int16_t w, int16_t h, int16_t t,
                              uint8_t color);
void ssd1306_draw_7seg_colon(int16_t x, int16_t y, int16_t h,
                              int16_t dot_size, uint8_t color);

/* ── Bitmap (1-bit, MSB first per byte, row-major) ─────────── */
void ssd1306_draw_bitmap(int16_t x, int16_t y, const uint8_t *bmp,
                          int16_t w, int16_t h, uint8_t color);

#endif /* SSD1306_H */
