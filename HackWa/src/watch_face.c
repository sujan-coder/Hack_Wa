/*
 * Watch Face / UI Rendering
 *
 * Display layout (128×64, Yellow-Blue OLED):
 *   Blue zone  :  y = 0 … 47   (48 px)  – main content
 *   Yellow zone:  y = 48 … 63  (16 px)  – status bar
 */
#include "watch_face.h"
#include "ssd1306.h"
#include "pw_store.h"
#include "ble_service.h"
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"

/* ── Day / month name tables ───────────────────────────────── */
static const char *DAY3[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *MON3[]   = {"Jan","Feb","Mar","Apr","May","Jun",
                                "Jul","Aug","Sep","Oct","Nov","Dec"};

/* ── Battery level (0–100 %) ───────────────────────────────── */
static uint8_t battery_pct = 85;

void watch_face_set_battery_pct(uint8_t pct)
{
    battery_pct = pct > 100 ? 100 : pct;
}

/* ── Icon bitmaps (MSB-first, row-major) ───────────────────── */

/* Bluetooth rune  5×7 */
static const uint8_t icon_bt[] = {
    0x20, 0x30, 0xA8, 0x60, 0xA8, 0x30, 0x20,
};

/* Bell  7×7 */
static const uint8_t icon_bell[] = {
    0x10, 0x38, 0x7C, 0x7C, 0x7C, 0xFE, 0x10,
};

/* ── Drawing helpers ───────────────────────────────────────── */

static void draw_battery_icon(int16_t x, int16_t y, uint8_t pct)
{
    /* Body outline 13×7 */
    ssd1306_draw_rect(x, y + 1, 13, 7, SSD1306_WHITE);
    /* Positive terminal nub */
    ssd1306_fill_rect(x + 13, y + 3, 2, 3, SSD1306_WHITE);
    /* Inner fill level */
    int fw = (int)pct * 9 / 100;
    if (fw > 9) fw = 9;
    if (fw > 0)
        ssd1306_fill_rect(x + 2, y + 3, fw, 3, SSD1306_WHITE);
}

static void draw_ble_icon(int16_t x, int16_t y, bool connected)
{
    ssd1306_draw_bitmap(x, y, icon_bt, 5, 7, SSD1306_WHITE);
    if (connected)
        ssd1306_fill_circle(x + 8, y + 3, 2, SSD1306_WHITE);
    else
        ssd1306_draw_circle(x + 8, y + 3, 2, SSD1306_WHITE);
}

static void draw_hud_corners(void)
{
    int16_t len = 8;
    /* Top-left */
    ssd1306_draw_hline(0, 0, len, SSD1306_WHITE);
    ssd1306_draw_vline(0, 0, len, SSD1306_WHITE);
    /* Top-right */
    ssd1306_draw_hline(SSD1306_WIDTH - len, 0, len, SSD1306_WHITE);
    ssd1306_draw_vline(SSD1306_WIDTH - 1, 0, len, SSD1306_WHITE);
    /* Bottom-left */
    ssd1306_draw_hline(0, 47, len, SSD1306_WHITE);
    ssd1306_draw_vline(0, 47 - len + 1, len, SSD1306_WHITE);
    /* Bottom-right */
    ssd1306_draw_hline(SSD1306_WIDTH - len, 47, len, SSD1306_WHITE);
    ssd1306_draw_vline(SSD1306_WIDTH - 1, 47 - len + 1, len, SSD1306_WHITE);
}

static void draw_seconds_bar(int16_t y, int secs)
{
    int16_t bx = 10, bw = 108, bh = 4;
    ssd1306_draw_rect(bx, y, bw, bh, SSD1306_WHITE);
    int fw = (secs > 0) ? ((bw - 2) * secs / 59) : 0;
    if (fw > bw - 2) fw = bw - 2;
    if (fw > 0)
        ssd1306_fill_rect(bx + 1, y + 1, fw, bh - 2, SSD1306_WHITE);
}

static void update_battery_simulation(void)
{
    static int64_t last_ts = 0;
    int64_t now = esp_timer_get_time();
    if (last_ts == 0) last_ts = now;
    if (now - last_ts > 300000000LL) {   /* every 5 min */
        last_ts = now;
        if (battery_pct > 5) battery_pct--;
    }
}

/* ── Splash ────────────────────────────────────────────────── */
void watch_face_splash(int load_pct)
{
    ssd1306_clear();

    /* Decorative rounded frame */
    ssd1306_draw_rounded_rect(4, 2, 120, 40, 3, SSD1306_WHITE);

    /* Title */
    ssd1306_draw_string_centered(8,  "HACKWA", SSD1306_WHITE, 2);

    /* Tagline */
    ssd1306_draw_string_centered(28, "Wearable v1.0", SSD1306_WHITE, 1);

    /* Small accent lines below frame */
    ssd1306_draw_hline(4, 44, 8, SSD1306_WHITE);
    ssd1306_draw_hline(116, 44, 8, SSD1306_WHITE);

    /* Loading bar */
    ssd1306_draw_rect(14, 50, 100, 8, SSD1306_WHITE);
    int fill = load_pct * 96 / 100;
    if (fill > 96) fill = 96;
    if (fill > 0)
        ssd1306_fill_rect(16, 52, fill, 4, SSD1306_WHITE);

    ssd1306_update();
}

/* ── Main clock face ───────────────────────────────────────── */
void watch_face_draw(const struct tm *t, bool ble_on, int notif_count)
{
    (void)ble_on;   /* shown in status bar instead */

    /* ---- HUD corner brackets ---- */
    draw_hud_corners();

    /* ---- large 7-segment time  HH:MM ---- */
    int dw = 16, dh = 24, dt = 3, gap = 3;
    int colon_w = 6;
    int total_w = 4 * dw + 3 * gap + colon_w;
    int x0 = (SSD1306_WIDTH - total_w) / 2;
    int y0 = 3;

    int h  = t->tm_hour;
    int m  = t->tm_min;
    int x  = x0;

    ssd1306_draw_7seg_digit(x, y0, h / 10, dw, dh, dt, SSD1306_WHITE);  x += dw + gap;
    ssd1306_draw_7seg_digit(x, y0, h % 10, dw, dh, dt, SSD1306_WHITE);  x += dw + gap;

    /* blinking colon */
    if (t->tm_sec % 2 == 0)
        ssd1306_draw_7seg_colon(x, y0, dh, 3, SSD1306_WHITE);
    x += colon_w;

    ssd1306_draw_7seg_digit(x, y0, m / 10, dw, dh, dt, SSD1306_WHITE);  x += dw + gap;
    ssd1306_draw_7seg_digit(x, y0, m % 10, dw, dh, dt, SSD1306_WHITE);

    /* ---- seconds (small, right-aligned) ---- */
    char secbuf[4];
    snprintf(secbuf, sizeof(secbuf), "%02d", t->tm_sec);
    ssd1306_draw_string(x + dw + 2, y0 + dh - 8, secbuf, SSD1306_WHITE, 1);

    /* ---- animated seconds progress bar ---- */
    draw_seconds_bar(30, t->tm_sec);

    /* ---- date line ---- */
    char datebuf[24];
    snprintf(datebuf, sizeof(datebuf), "%s  %s %d",
             DAY3[t->tm_wday % 7], MON3[t->tm_mon % 12], t->tm_mday);
    ssd1306_draw_string_centered(37, datebuf, SSD1306_WHITE, 1);

    /* ---- notification indicator (bell icon + count) ---- */
    if (notif_count > 0) {
        ssd1306_draw_bitmap(100, 37, icon_bell, 7, 7, SSD1306_WHITE);
        char nb[12];
        snprintf(nb, sizeof(nb), "%d", notif_count);
        ssd1306_draw_string(109, 37, nb, SSD1306_WHITE, 1);
    }
}

/* ── Notification detail ───────────────────────────────────── */
void watch_face_draw_notif(int index)
{
    char title[32], body[64];
    ble_get_notif(index, title, sizeof(title), body, sizeof(body));

    int total = ble_get_notif_count();
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "NOTIF %d/%d", index + 1, total);
    ssd1306_draw_string(2, 0, hdr, SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    /* title */
    ssd1306_draw_string(2, 12, title, SSD1306_WHITE, 1);

    /* body – word-wrap at ~21 chars per line */
    int y = 24;
    int len = strlen(body);
    int pos = 0;
    while (pos < len && y < 46) {
        int chunk = (len - pos > 21) ? 21 : (len - pos);
        char line[22];
        memcpy(line, body + pos, chunk);
        line[chunk] = '\0';
        ssd1306_draw_string(2, y, line, SSD1306_WHITE, 1);
        pos += chunk;
        y += 10;
    }
}

/* ── Stopwatch ─────────────────────────────────────────────── */
void watch_face_draw_stopwatch(int mins, int secs, int cs, bool running)
{
    ssd1306_draw_string(2, 0, "STOPWATCH", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    /* MM:SS large */
    char tbuf[10];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mins, secs);
    ssd1306_draw_string_centered(14, tbuf, SSD1306_WHITE, 2);

    /* .cc centiseconds */
    char csbuf[6];
    snprintf(csbuf, sizeof(csbuf), ".%02d", cs);
    ssd1306_draw_string(92, 22, csbuf, SSD1306_WHITE, 1);

    /* running indicator */
    if (running) {
        ssd1306_fill_circle(64, 38, 3, SSD1306_WHITE);
    } else {
        ssd1306_draw_circle(64, 38, 3, SSD1306_WHITE);
    }

    /* button hints */
    ssd1306_draw_string(2,  40, "SEL:go", SSD1306_WHITE, 1);
    ssd1306_draw_string(76, 40, "DN:rst", SSD1306_WHITE, 1);
}

/* ── Password list ─────────────────────────────────────────── */
void watch_face_draw_pw_list(int cursor, int total)
{
    ssd1306_draw_string(2, 0, "PASSWORD MANAGER", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    if (total == 0) {
        ssd1306_draw_string_centered(20, "No passwords", SSD1306_WHITE, 1);
        ssd1306_draw_string_centered(32, "Use BLE to add", SSD1306_WHITE, 1);
        return;
    }

    /* show up to 4 entries centred on cursor */
    int visible = 4;
    int start = cursor - 1;
    if (start < 0) start = 0;
    if (start + visible > total) start = total - visible;
    if (start < 0) start = 0;

    /* find the slot indices that actually have data */
    int slots[PW_MAX_SLOTS];
    int cnt = 0;
    char lbl[PW_MAX_LABEL];
    for (int i = 0; i < PW_MAX_SLOTS && cnt < total; i++) {
        if (pw_store_get_label(i, lbl, sizeof(lbl)))
            slots[cnt++] = i;
    }

    for (int i = start; i < start + visible && i < cnt; i++) {
        int y = 12 + (i - start) * 10;
        pw_store_get_label(slots[i], lbl, sizeof(lbl));

        if (i == cursor) {
            ssd1306_fill_rect(0, y - 1, 128, 10, SSD1306_WHITE);
            ssd1306_draw_string(10, y, lbl, SSD1306_BLACK, 1);
        } else {
            ssd1306_draw_string(10, y, lbl, SSD1306_WHITE, 1);
        }
    }

    /* scroll indicator */
    if (total > visible) {
        int bar_h = (visible * 36) / total;
        if (bar_h < 4) bar_h = 4;
        int bar_y = 12 + (start * 36) / total;
        ssd1306_fill_rect(126, bar_y, 2, bar_h, SSD1306_WHITE);
    }
}

/* ── "Typing…" screen ──────────────────────────────────────── */
void watch_face_draw_typing(void)
{
    ssd1306_draw_string_centered(10, "Sending", SSD1306_WHITE, 2);
    ssd1306_draw_string_centered(30, "password...", SSD1306_WHITE, 1);

    /* animated bar */
    ssd1306_draw_rect(14, 42, 100, 6, SSD1306_WHITE);
    ssd1306_fill_rect(16, 44, 60, 2, SSD1306_WHITE);
}

/* ── Password reveal screen ────────────────────────────────── */
void watch_face_draw_reveal(const char *label, const char *password, bool ble_ok)
{
    /* header */
    ssd1306_draw_string(2, 0, label, SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    /* password text – word-wrap at 21 chars per line */
    int len = strlen(password);
    int pos = 0;
    int y = 12;
    while (pos < len && y < 32) {
        int chunk = (len - pos > 21) ? 21 : (len - pos);
        char line[22];
        memcpy(line, password + pos, chunk);
        line[chunk] = '\0';
        ssd1306_draw_string(2, y, line, SSD1306_WHITE, 1);
        pos += chunk;
        y += 10;
    }

    /* button hints */
    if (ble_ok) {
        ssd1306_draw_string(2,  34, "SEL:type", SSD1306_WHITE, 1);
        ssd1306_draw_string(62, 34, "UP:enter", SSD1306_WHITE, 1);
        ssd1306_draw_string(2,  42, "DN:ok",    SSD1306_WHITE, 1);
    } else {
        ssd1306_draw_string(2,  34, "BT not connected",  SSD1306_WHITE, 1);
    }
    ssd1306_draw_string(76, 42, "BK:back", SSD1306_WHITE, 1);
}

/* ── "JOB DONE" screen ───────────────────────────────────────── */
void watch_face_draw_done(void)
{
    ssd1306_draw_string_centered(20, "JOB DONE", SSD1306_WHITE, 2);
}
/* ── Find My Phone screen ─────────────────────────────────── */
void watch_face_draw_find_phone(int anim_frame)
{
    ssd1306_draw_string(2, 0, "FIND MY PHONE", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    /* Animated phone icon: pulsing circle + phone outline */
    int pulse = (anim_frame / 3) % 4;   /* 0-3 pulse cycle */

    /* Phone body (rectangle) */
    ssd1306_draw_rounded_rect(52, 14, 24, 28, 3, SSD1306_WHITE);
    /* Screen area */
    ssd1306_draw_rect(55, 18, 18, 18, SSD1306_WHITE);
    /* Home button dot */
    ssd1306_fill_circle(64, 39, 1, SSD1306_WHITE);

    /* Radiating rings (animate outward) */
    if (pulse >= 1)
        ssd1306_draw_circle(64, 27, 18, SSD1306_WHITE);
    if (pulse >= 2)
        ssd1306_draw_circle(64, 27, 22, SSD1306_WHITE);
    if (pulse >= 3)
        ssd1306_draw_circle(64, 27, 26, SSD1306_WHITE);

    /* Blinking "RINGING" text */
    if ((anim_frame / 5) % 2 == 0)
        ssd1306_draw_string_centered(44, "RINGING...", SSD1306_WHITE, 1);
}
/* ── Status bar (yellow zone, y 48‥63) ─────────────────────── */
void watch_face_draw_status(const char *mode_label, bool ble_connected)
{
    /* background strip */
    ssd1306_fill_rect(0, 49, 128, 15, SSD1306_BLACK);
    ssd1306_draw_hline(0, 49, 128, SSD1306_WHITE);

    /* Battery simulation tick */
    update_battery_simulation();

    /* Battery icon */
    draw_battery_icon(3, 51, battery_pct);

    /* BLE icon */
    draw_ble_icon(22, 52, ble_connected);

    /* Tiny mode badge (single char in rounded box) */
    ssd1306_draw_rounded_rect(116, 51, 11, 11, 2, SSD1306_WHITE);
    ssd1306_draw_char(119, 53, mode_label[0], SSD1306_WHITE, 1);
}

/* ── Power Tools menu ──────────────────────────────────────── */
void watch_face_draw_tools_menu(int cursor)
{
    static const char *items[] = {
        "Always-On Display",
        "Countdown Timer",
        "BLE Jam",
        "BLE Scanner"
    };
    static const int count = 4;

    ssd1306_draw_string(2, 0, "POWER TOOLS", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    for (int i = 0; i < count; i++) {
        int y = 12 + i * 9;
        if (i == cursor) {
            ssd1306_fill_rect(0, y - 1, 128, 9, SSD1306_WHITE);
            ssd1306_draw_string(10, y, items[i], SSD1306_BLACK, 1);
        } else {
            ssd1306_draw_string(10, y, items[i], SSD1306_WHITE, 1);
        }
    }

    ssd1306_draw_string(2,  44, "SEL:open", SSD1306_WHITE, 1);
    ssd1306_draw_string(76, 44, "BK:back", SSD1306_WHITE, 1);
}

/* ── Always-On Display ─────────────────────────────────────── */
void watch_face_draw_aod(const struct tm *t, int timer_mins_left,
                         bool timer_set, bool active)
{
    /* Header */
    ssd1306_draw_string(2, 0,
        active ? "\x7F AOD ACTIVE" : "  AOD OFF",
        SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    /* Large time HH:MM */
    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
    ssd1306_draw_string_centered(13, tbuf, SSD1306_WHITE, 2);

    /* Seconds */
    char secbuf[4];
    snprintf(secbuf, sizeof(secbuf), ":%02d", t->tm_sec);
    ssd1306_draw_string(92, 21, secbuf, SSD1306_WHITE, 1);

    /* Date line */
    char datebuf[24];
    snprintf(datebuf, sizeof(datebuf), "%s %s %d",
             DAY3[t->tm_wday % 7], MON3[t->tm_mon % 12], t->tm_mday);
    ssd1306_draw_string_centered(32, datebuf, SSD1306_WHITE, 1);

    /* Timer info */
    if (timer_set) {
        char tmbuf[24];
        snprintf(tmbuf, sizeof(tmbuf), "Timer: %d min", timer_mins_left);
        ssd1306_draw_string_centered(43, tmbuf, SSD1306_WHITE, 1);
    } else {
        ssd1306_draw_string_centered(43, "Timer: infinite", SSD1306_WHITE, 1);
    }
}

/* ── Countdown Timer ───────────────────────────────────────── */
void watch_face_draw_timer(int remaining_secs, int preset_secs, bool running)
{
    ssd1306_draw_string(2, 0, "COUNTDOWN", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    int mins = remaining_secs / 60;
    int secs = remaining_secs % 60;

    if (remaining_secs == 0 && !running) {
        /* Timer expired – flash DONE */
        ssd1306_draw_string_centered(14, "DONE!", SSD1306_WHITE, 2);
    } else {
        /* Large MM:SS */
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mins, secs);
        ssd1306_draw_string_centered(14, tbuf, SSD1306_WHITE, 2);
    }

    /* Progress bar */
    int16_t bx = 10, bw = 108, bh = 6;
    ssd1306_draw_rect(bx, 33, bw, bh, SSD1306_WHITE);
    if (preset_secs > 0) {
        int fw = (remaining_secs * (bw - 2)) / preset_secs;
        if (fw > bw - 2) fw = bw - 2;
        if (fw > 0)
            ssd1306_fill_rect(bx + 1, 34, fw, bh - 2, SSD1306_WHITE);
    }

    /* Running indicator */
    if (running)
        ssd1306_fill_circle(64, 44, 3, SSD1306_WHITE);
    else
        ssd1306_draw_circle(64, 44, 3, SSD1306_WHITE);
}

/* ── BLE Jam ───────────────────────────────────────────────── */
void watch_face_draw_ble_jam(bool active, uint32_t pkt_count, int elapsed_secs)
{
    ssd1306_draw_string(2, 0, "BLE JAM", SSD1306_WHITE, 1);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    if (active) {
        ssd1306_fill_rect(14, 13, 100, 12, SSD1306_WHITE);
        ssd1306_draw_string_centered(15, "ACTIVE", SSD1306_BLACK, 1);
    } else {
        ssd1306_draw_rect(14, 13, 100, 12, SSD1306_WHITE);
        ssd1306_draw_string_centered(15, "STOPPED", SSD1306_WHITE, 1);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "Pkts: %lu", (unsigned long)pkt_count);
    ssd1306_draw_string(2, 28, buf, SSD1306_WHITE, 1);

    snprintf(buf, sizeof(buf), "Time: %02d:%02d",
             elapsed_secs / 60, elapsed_secs % 60);
    ssd1306_draw_string(2, 38, buf, SSD1306_WHITE, 1);

    ssd1306_draw_string(68, 38, "SEL:go", SSD1306_WHITE, 1);
}

/* ── BLE Scanner ───────────────────────────────────────────── */
void watch_face_draw_ble_scan(int cursor, int total, bool scanning)
{
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "BLE SCAN (%d)", total);
    ssd1306_draw_string(2, 0, hdr, SSD1306_WHITE, 1);
    if (scanning)
        ssd1306_fill_circle(120, 4, 3, SSD1306_WHITE);
    ssd1306_draw_hline(0, 9, 128, SSD1306_WHITE);

    if (total == 0) {
        ssd1306_draw_string_centered(20,
            scanning ? "Scanning..." : "Press SEL",
            SSD1306_WHITE, 1);
        if (!scanning)
            ssd1306_draw_string_centered(30, "to start scan",
                                         SSD1306_WHITE, 1);
        return;
    }

    /* Show up to 4 results centred on cursor */
    int visible = 4;
    int start = cursor - 1;
    if (start < 0) start = 0;
    if (start + visible > total) start = total - visible;
    if (start < 0) start = 0;

    const ble_scan_result_t *res = ble_scan_get_results();

    for (int i = start; i < start + visible && i < total; i++) {
        int y = 12 + (i - start) * 9;
        char line[24];

        if (res[i].name[0] != '?' && res[i].name[0] != '\0') {
            snprintf(line, sizeof(line), "%.10s %ddBm",
                     res[i].name, (int)res[i].rssi);
        } else {
            snprintf(line, sizeof(line), "%02X:%02X:%02X %ddBm",
                     res[i].addr[5], res[i].addr[4], res[i].addr[3],
                     (int)res[i].rssi);
        }

        if (i == cursor) {
            ssd1306_fill_rect(0, y - 1, 128, 9, SSD1306_WHITE);
            ssd1306_draw_string(2, y, line, SSD1306_BLACK, 1);
        } else {
            ssd1306_draw_string(2, y, line, SSD1306_WHITE, 1);
        }
    }

    /* Scroll indicator bar */
    if (total > visible) {
        int bar_h = (visible * 36) / total;
        if (bar_h < 4) bar_h = 4;
        int bar_y = 12 + (start * 36) / total;
        ssd1306_fill_rect(126, bar_y, 2, bar_h, SSD1306_WHITE);
    }
}
