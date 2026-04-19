#ifndef WATCH_FACE_H
#define WATCH_FACE_H

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/* ── Main clock face ───────────────────────────────────────── */
void watch_face_draw(const struct tm *t, bool ble_on, int notif_count);

/* ── Notification view ─────────────────────────────────────── */
void watch_face_draw_notif(int index);

/* ── Stopwatch view ───────────────────────────────────────── */
void watch_face_draw_stopwatch(int mins, int secs, int cs, bool running);

/* ── HID-mode screens ──────────────────────────────────────── */
void watch_face_draw_pw_list(int cursor, int total);
void watch_face_draw_reveal(const char *label, const char *password, bool ble_ok);
void watch_face_draw_typing(void);
void watch_face_draw_done(void);

/* ── Find My Phone ─────────────────────────────────────────── */
void watch_face_draw_find_phone(int anim_frame);

/* ── Status bar (always at bottom, yellow zone y=48..63) ──── */
void watch_face_draw_status(const char *mode_label, bool ble_connected);

/* ── Splash screen ─────────────────────────────────────────── */
void watch_face_splash(int load_pct);

/* ── Battery level (call from main or ADC task) ──────────── */
void watch_face_set_battery_pct(uint8_t pct);

/* ── Power Tools menu ──────────────────────────────────────── */
void watch_face_draw_tools_menu(int cursor);

/* ── Always-On Display ─────────────────────────────────────── */
void watch_face_draw_aod(const struct tm *t, int timer_mins_left,
                         bool timer_set, bool active);

/* ── Countdown Timer ───────────────────────────────────────── */
void watch_face_draw_timer(int remaining_secs, int preset_secs, bool running);

/* ── BLE Jam ───────────────────────────────────────────────── */
void watch_face_draw_ble_jam(bool active, uint32_t pkt_count, int elapsed_secs);

/* ── BLE Scanner ───────────────────────────────────────────── */
void watch_face_draw_ble_scan(int cursor, int total, bool scanning);

#endif /* WATCH_FACE_H */
