/*
 * HackWa – ESP32-C6 Wearable Watch Firmware
 *
 * WATCH MODE  – digital clock, BLE time sync, notifications (via NUS AT-commands)
 * HID MODE    – BLE HID keyboard password manager (single-click unlock)
 *
 * Press BACK (GPIO21) on the main screen to toggle between modes.
 *
 * Pin wiring:
 *   OLED SDA → GPIO22 (D4)      Button UP     → GPIO0  (D0)
 *   OLED SCL → GPIO23 (D5)      Button DOWN   → GPIO1  (D1)
 *                                Button SELECT → GPIO2  (D2)
 *                                Button BACK   → GPIO21 (D3)
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ssd1306.h"
#include "buttons.h"
#include "watch_face.h"
#include "ble_service.h"
#include "pw_store.h"
#include "nvs.h"

#define TAG "MAIN"

/* ── NVS persistence ────────────────────────────────────────── */
#define NVS_NS       "hackwa"
#define NVS_KEY_TMOUT "scr_tmout"
#define NVS_KEY_BRT   "brightness"

/* ── GPIO assignments ──────────────────────────────────────── */
#define OLED_SDA  22
#define OLED_SCL  23

/* ── Application modes ─────────────────────────────────────── */
typedef enum { MODE_WATCH, MODE_HID } app_mode_t;

/* Watch sub-screens */
typedef enum {
    WSCR_TIME, WSCR_NOTIF, WSCR_STOPWATCH, WSCR_FINDPHONE,
    WSCR_TOOLS, WSCR_AOD, WSCR_BLE_JAM, WSCR_BLE_SCAN, WSCR_TIMER
} watch_screen_t;

/* Find-phone animation state */
static int find_phone_anim = 0;

/* HID sub-screens */
typedef enum { HSCR_LIST, HSCR_TYPING, HSCR_DONE } hid_screen_t;

static app_mode_t     mode      = MODE_WATCH;
static watch_screen_t watch_scr = WSCR_TIME;
static hid_screen_t   hid_scr   = HSCR_LIST;
static int            hid_cursor  = 0;
static int            notif_idx   = 0;

/* Stopwatch state */
static bool     sw_running = false;
static int64_t  sw_start   = 0;     /* esp_timer µs when started */
static int64_t  sw_elapsed = 0;     /* accumulated µs            */

/* ── Tools / Power Tools state ─────────────────────────────── */
#define TOOL_COUNT 4
static int tools_cursor = 0;

/* Always-On Display */
static bool    aod_active = false;
static int     aod_timer_mins = 30;      /* 0=infinite, else countdown minutes */
static int64_t aod_start_us = 0;

/* Countdown Timer */
static int     timer_preset_secs = 300;  /* 5 min default */
static bool    timer_running = false;
static int64_t timer_start_us = 0;
static int64_t timer_remain_us = 0;

/* BLE Jam elapsed tracking */
static int64_t jam_start_us = 0;

/* BLE Scan cursor */
static int scan_cursor = 0;

/* Display brightness (default=medium to save OLED power) */
static uint8_t  brightness_level = 1;  /* 0=dim,1=med,2=bright */
static const uint8_t brightness_val[] = {0x01, 0x80, 0xFF};

/* Dirty-screen tracking: skip I2C push when nothing changed */
static int last_drawn_sec = -1;         /* last second we drew the clock */
static int last_drawn_min = -1;         /* last minute (catches BLE time sync) */
static bool screen_dirty   = true;      /* force first draw              */

/* OLED sleep/wake */
#define SLEEP_TIMEOUT_MS  30000          /* default 30 seconds of inactivity */
static int     sleep_timeout_ms = SLEEP_TIMEOUT_MS; /* adjustable via AT+ST   */
static int64_t  last_activity_us = 0;    /* last button/notif timestamp */
static bool     oled_sleeping    = false;

/* Find Watch state (triggered from companion app) */
static bool     find_watch_active = false;
static int      find_watch_blinks = 0;

/* ── Force screen redraw (called from BLE when time is set) ── */
void force_screen_redraw(void)
{
    screen_dirty = true;
    last_drawn_sec = -1;
    last_drawn_min = -1;
}

/* ── Fast-poll button scanning during slow delays ──────────── */
static void delay_with_scan(int ms)
{
    while (ms > 0) {
        int d = (ms > 10) ? 10 : ms;
        vTaskDelay(pdMS_TO_TICKS(d));
        ms -= d;
        buttons_update();
    }
}

/* ── Mode switch ───────────────────────────────────────────── */
static void switch_mode(void)
{
    if (mode == MODE_WATCH) {
        /* Notify companion app before disconnect so it stops auto-reconnect */
        ble_send_mode_notify("HID");
        mode       = MODE_HID;
        hid_scr    = HSCR_LIST;
        hid_cursor = 0;
        ble_switch_hid_mode();
        ESP_LOGI(TAG, "→ HID MODE");
    } else {
        mode      = MODE_WATCH;
        watch_scr = WSCR_TIME;
        ble_switch_watch_mode();
        ESP_LOGI(TAG, "→ WATCH MODE");
    }
}

/* ── Watch mode handler (returns false if nothing drawn) ───── */
static bool handle_watch(button_event_t up, button_event_t dn,
                         button_event_t sel, button_event_t back)
{
    switch (watch_scr) {

    case WSCR_TIME:
        /* UP → open stopwatch */
        if (up == BTN_EVT_PRESS) {
            watch_scr = WSCR_STOPWATCH;
        }

        /* DOWN → Find My Phone */
        if (dn == BTN_EVT_PRESS) {
            if (ble_is_connected()) {
                watch_scr = WSCR_FINDPHONE;
                find_phone_anim = 0;
                ble_send_find_phone();
            }
        }

        /* SELECT → show notifications (if any) */
        if (sel == BTN_EVT_PRESS && ble_get_notif_count() > 0) {
            watch_scr = WSCR_NOTIF;
            notif_idx = 0;
        }

        /* SELECT long press → open Power Tools menu */
        if (sel == BTN_EVT_LONG_PRESS) {
            watch_scr = WSCR_TOOLS;
            tools_cursor = 0;
        }

        /* BACK → switch to HID mode (single press on main screen) */
        if (back == BTN_EVT_PRESS) {
            switch_mode();
            return false;   /* skip drawing this tick */
        }

        /* draw clock face (only once per second to save I2C + CPU) */
        {
            time_t now; time(&now);
            struct tm ti; localtime_r(&now, &ti);
            if (ti.tm_sec == last_drawn_sec &&
                ti.tm_min == last_drawn_min && !screen_dirty) {
                return false;   /* nothing changed – keep previous framebuffer */
            }
            last_drawn_sec = ti.tm_sec;
            last_drawn_min = ti.tm_min;
            screen_dirty   = false;
            watch_face_draw(&ti, ble_is_connected(), ble_get_notif_count());
        }
        break;

    case WSCR_NOTIF:
        /* UP/DOWN → scroll notifications */
        if (up == BTN_EVT_PRESS && notif_idx > 0)
            notif_idx--;
        if (dn == BTN_EVT_PRESS && notif_idx < ble_get_notif_count() - 1)
            notif_idx++;
        /* SELECT → clear all notifications */
        if (sel == BTN_EVT_PRESS) {
            notif_count_clear();
            watch_scr = WSCR_TIME;
        }
        if (back == BTN_EVT_PRESS)
            watch_scr = WSCR_TIME;

        watch_face_draw_notif(notif_idx);
        break;

    case WSCR_FINDPHONE:
        /* BACK or SELECT → cancel / return */
        if (back == BTN_EVT_PRESS || sel == BTN_EVT_PRESS) {
            watch_scr = WSCR_TIME;
        }
        find_phone_anim++;
        watch_face_draw_find_phone(find_phone_anim);
        break;

    case WSCR_STOPWATCH: {
        /* SELECT → start / stop */
        if (sel == BTN_EVT_PRESS) {
            if (!sw_running) {
                sw_running = true;
                sw_start = esp_timer_get_time() - sw_elapsed;
            } else {
                sw_running = false;
                sw_elapsed = esp_timer_get_time() - sw_start;
            }
        }
        /* DOWN → reset (only while stopped) */
        if (dn == BTN_EVT_PRESS && !sw_running) {
            sw_elapsed = 0;
        }
        /* BACK → return to clock */
        if (back == BTN_EVT_PRESS) {
            watch_scr = WSCR_TIME;
        }

        int64_t elapsed_us = sw_running
            ? (esp_timer_get_time() - sw_start)
            : sw_elapsed;
        int total_cs = (int)(elapsed_us / 10000);
        int mins = total_cs / 6000;
        int secs = (total_cs / 100) % 60;
        int cs   = total_cs % 100;
        watch_face_draw_stopwatch(mins, secs, cs, sw_running);
        break;
    }

    /* ── Power Tools screens ──────────────────────────────────── */

    case WSCR_TOOLS:
        if (up == BTN_EVT_PRESS && tools_cursor > 0) tools_cursor--;
        if (dn == BTN_EVT_PRESS && tools_cursor < TOOL_COUNT - 1) tools_cursor++;
        if (back == BTN_EVT_PRESS) watch_scr = WSCR_TIME;
        if (sel == BTN_EVT_PRESS) {
            switch (tools_cursor) {
                case 0: watch_scr = WSCR_AOD;      break;
                case 1: watch_scr = WSCR_TIMER;    break;
                case 2: watch_scr = WSCR_BLE_JAM;  break;
                case 3: watch_scr = WSCR_BLE_SCAN; break;
            }
        }
        watch_face_draw_tools_menu(tools_cursor);
        break;

    case WSCR_AOD:
        /* UP/DOWN → adjust AOD auto-off timer (0 = infinite) */
        if (up == BTN_EVT_PRESS) {
            aod_timer_mins += 5;
            if (aod_timer_mins > 120) aod_timer_mins = 0;
        }
        if (dn == BTN_EVT_PRESS) {
            aod_timer_mins -= 5;
            if (aod_timer_mins < 0) aod_timer_mins = 120;
        }
        /* SELECT → toggle AOD on/off */
        if (sel == BTN_EVT_PRESS) {
            aod_active = !aod_active;
            if (aod_active) aod_start_us = esp_timer_get_time();
        }
        /* BACK → return to tools (AOD stays active if enabled) */
        if (back == BTN_EVT_PRESS) {
            watch_scr = WSCR_TOOLS;
        }
        /* AOD timer expiry check */
        {
            int remaining_mins = aod_timer_mins;
            if (aod_active && aod_timer_mins > 0) {
                int64_t elapsed_us = esp_timer_get_time() - aod_start_us;
                int elapsed_mins = (int)(elapsed_us / 60000000LL);
                remaining_mins = aod_timer_mins - elapsed_mins;
                if (remaining_mins <= 0) {
                    aod_active = false;
                    remaining_mins = 0;
                }
            }
            time_t now; time(&now);
            struct tm ti; localtime_r(&now, &ti);
            watch_face_draw_aod(&ti, remaining_mins,
                                aod_timer_mins > 0, aod_active);
        }
        break;

    case WSCR_BLE_JAM:
        if (sel == BTN_EVT_PRESS) {
            if (ble_jam_is_active()) {
                ble_stop_jam();
            } else {
                jam_start_us = esp_timer_get_time();
                ble_start_jam();
            }
        }
        if (back == BTN_EVT_PRESS) {
            if (ble_jam_is_active()) ble_stop_jam();
            watch_scr = WSCR_TOOLS;
        }
        {
            int elapsed_secs = 0;
            if (ble_jam_is_active())
                elapsed_secs = (int)((esp_timer_get_time() - jam_start_us)
                                     / 1000000LL);
            watch_face_draw_ble_jam(ble_jam_is_active(),
                                    ble_jam_get_pkt_count(), elapsed_secs);
        }
        break;

    case WSCR_BLE_SCAN:
        if (sel == BTN_EVT_PRESS) {
            if (ble_scan_is_active()) {
                ble_stop_scan();
            } else {
                scan_cursor = 0;
                ble_start_scan();
            }
        }
        {
            int cnt = ble_scan_get_count();
            if (up == BTN_EVT_PRESS && scan_cursor > 0) scan_cursor--;
            if (dn == BTN_EVT_PRESS && scan_cursor < cnt - 1) scan_cursor++;
            if (back == BTN_EVT_PRESS) {
                if (ble_scan_is_active()) ble_stop_scan();
                watch_scr = WSCR_TOOLS;
            }
            watch_face_draw_ble_scan(scan_cursor, cnt,
                                     ble_scan_is_active());
        }
        break;

    case WSCR_TIMER: {
        /* UP/DOWN → adjust preset (when stopped) */
        if (!timer_running) {
            if (up == BTN_EVT_PRESS) {
                timer_preset_secs += 60;
                if (timer_preset_secs > 5400) timer_preset_secs = 60;
            }
            if (dn == BTN_EVT_PRESS) {
                timer_preset_secs -= 60;
                if (timer_preset_secs < 60) timer_preset_secs = 5400;
            }
        }
        /* SELECT → start / pause */
        if (sel == BTN_EVT_PRESS) {
            if (!timer_running) {
                timer_running = true;
                timer_start_us = esp_timer_get_time();
                if (timer_remain_us <= 0)
                    timer_remain_us = (int64_t)timer_preset_secs * 1000000LL;
            } else {
                timer_running = false;
                int64_t elapsed = esp_timer_get_time() - timer_start_us;
                timer_remain_us -= elapsed;
                if (timer_remain_us < 0) timer_remain_us = 0;
            }
        }
        /* BACK → exit */
        if (back == BTN_EVT_PRESS) {
            timer_running = false;
            timer_remain_us = 0;
            watch_scr = WSCR_TOOLS;
        }
        /* Calculate remaining seconds */
        int remaining = timer_preset_secs;
        if (timer_running) {
            int64_t elapsed = esp_timer_get_time() - timer_start_us;
            int64_t left = timer_remain_us - elapsed;
            if (left <= 0) {
                remaining = 0;
                timer_running = false;
            } else {
                remaining = (int)(left / 1000000LL);
            }
        } else if (timer_remain_us > 0) {
            remaining = (int)(timer_remain_us / 1000000LL);
        }
        watch_face_draw_timer(remaining, timer_preset_secs, timer_running);
        break;
    }

    } /* switch */
    return true;   /* drew something */
}

/* ── HID mode handler ──────────────────────────────────────── */
static void handle_hid(button_event_t up, button_event_t dn,
                       button_event_t sel, button_event_t back)
{
    int count = pw_store_count();

    switch (hid_scr) {

    case HSCR_LIST:
        /* BACK → return to watch mode (single press) */
        if (back == BTN_EVT_PRESS) {
            switch_mode();
            return;   /* skip drawing this tick */
        }

        if (dn == BTN_EVT_PRESS && hid_cursor > 0)           hid_cursor--;
        if (up == BTN_EVT_PRESS && hid_cursor < count - 1)   hid_cursor++;

        /* SELECT → type the selected password + Enter via BLE HID */
        if (sel == BTN_EVT_PRESS && count > 0) {
            int slot = -1, n = 0;
            for (int i = 0; i < PW_MAX_SLOTS && n <= hid_cursor; i++) {
                char lbl[PW_MAX_LABEL];
                if (pw_store_get_label(i, lbl, sizeof(lbl))) {
                    if (n == hid_cursor) { slot = i; break; }
                    n++;
                }
            }

            if (slot >= 0) {
                char pw[PW_MAX_PASS];
                pw_store_get_pass(slot, pw, sizeof(pw));

                if (ble_hid_is_connected()) {
                    /* BLE HID path */
                    hid_scr = HSCR_TYPING;
                    ssd1306_clear();
                    watch_face_draw_typing();
                    watch_face_draw_status("HID", true);
                    ssd1306_update();

                    ble_hid_type_string(pw);
                    ble_hid_send_enter();

                    hid_scr = HSCR_DONE;
                    return;
                } else {
                    /* USB serial fallback – for when using PC script */
                    printf("HACKWA_PW:%s\n", pw);
                    fflush(stdout);
                    ESP_LOGI(TAG, "Password sent via USB serial (fallback)");
                    hid_scr = HSCR_DONE;
                    return;
                }
            }
        }

        watch_face_draw_pw_list(hid_cursor, count);
        break;

    case HSCR_DONE: {
        /* Show JOB DONE for 1.5 seconds then auto-return to list */
        watch_face_draw_done();
        ssd1306_update();
        vTaskDelay(pdMS_TO_TICKS(1500));
        hid_scr = HSCR_LIST;
        break;
    }

    case HSCR_TYPING:
        watch_face_draw_typing();
        break;
    }
}

/* ── Functions called from BLE service via extern ──────────── */
void trigger_find_watch(void)
{
    find_watch_active = true;
    find_watch_blinks = 0;
    last_activity_us = esp_timer_get_time();  /* wake display */
    if (oled_sleeping) {
        oled_sleeping = false;
        ssd1306_display_on(true);
        ssd1306_set_contrast(brightness_val[brightness_level]);
    }
    ESP_LOGI(TAG, "Find Watch: OLED blink started");
}

void set_screen_timeout(int seconds)
{
    sleep_timeout_ms = seconds * 1000;
    /* Persist to NVS so it survives reboot */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_TMOUT, seconds);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Screen timeout: %d s (saved)", seconds);
}

void set_brightness(int level)
{
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    brightness_level = (uint8_t)level;
    ssd1306_set_contrast(brightness_val[brightness_level]);
    /* Persist to NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_BRT, level);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Brightness: %d (saved)", level);
}

/* ── Entry point ───────────────────────────────────────────── */
void app_main(void)
{
    /* ---- NVS ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* ---- peripherals ---- */
    ssd1306_init(OLED_SDA, OLED_SCL);
    buttons_init();
    pw_store_init();

    setenv("TZ", "UTC0", 1);
    tzset();

    /* ---- load saved screen timeout from NVS ---- */
    {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            int32_t saved = 0;
            if (nvs_get_i32(h, NVS_KEY_TMOUT, &saved) == ESP_OK &&
                saved >= 5 && saved <= 300) {
                sleep_timeout_ms = (int)saved * 1000;
                ESP_LOGI(TAG, "Loaded screen timeout: %d s", (int)saved);
            }
            int32_t brt = -1;
            if (nvs_get_i32(h, NVS_KEY_BRT, &brt) == ESP_OK &&
                brt >= 0 && brt <= 2) {
                brightness_level = (uint8_t)brt;
                ESP_LOGI(TAG, "Loaded brightness: %d", (int)brt);
            }
            nvs_close(h);
        }
    }

    /* ---- default password (slot 0, only if empty) ---- */
    {
        char lbl[PW_MAX_LABEL];
        if (!pw_store_get_label(0, lbl, sizeof(lbl))) {
            pw_store_set(0, "Default", "password7383873");
            ESP_LOGI(TAG, "Default password stored in slot 0");
        }
    }

    /* ---- animated splash ---- */
    for (int pct = 0; pct <= 100; pct += 2) {
        watch_face_splash(pct);
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    /* ---- BLE (starts in watch mode) ---- */
    ble_service_init();

    /* initialise activity timer */
    last_activity_us = esp_timer_get_time();

    ESP_LOGI(TAG, "HackWa ready");

    /* ════════════════════════════════════════════════════════════
     *  Main loop  (~20 Hz)
     * ════════════════════════════════════════════════════════════*/
    while (1) {
        buttons_update();

        /* consume all button events once per tick */
        button_event_t ev_up   = buttons_get_event(BTN_UP);
        button_event_t ev_dn   = buttons_get_event(BTN_DOWN);
        button_event_t ev_sel  = buttons_get_event(BTN_SELECT);
        button_event_t ev_back = buttons_get_event(BTN_BACK);

        /* ---- detect activity (any button press or new notification) ---- */
        bool any_btn = (ev_up  != BTN_EVT_NONE || ev_dn  != BTN_EVT_NONE ||
                        ev_sel != BTN_EVT_NONE || ev_back != BTN_EVT_NONE);
        bool got_notif = ble_has_new_notif();

        if (any_btn || got_notif) {
            last_activity_us = esp_timer_get_time();
            screen_dirty = true;   /* force redraw on any input */
            if (oled_sleeping) {
                oled_sleeping = false;
                ssd1306_display_on(true);
                ssd1306_set_contrast(brightness_val[brightness_level]);
                ESP_LOGI(TAG, "OLED wake");
                /* eat the wake-up press so it doesn't trigger an action */
                if (any_btn && !got_notif) {
                    ev_up = ev_dn = ev_sel = ev_back = BTN_EVT_NONE;
                }
            }
            /* If woken by notification, switch to notif view */
            if (got_notif && mode == MODE_WATCH) {
                watch_scr = WSCR_NOTIF;
                notif_idx = 0;
            }
        }

        /* ---- auto-sleep (shorter timeout in HID mode for battery) ---- */
        int effective_timeout_ms = sleep_timeout_ms;
        if (mode == MODE_HID && sleep_timeout_ms > 10000)
            effective_timeout_ms = 10000;   /* cap at 10 s in HID mode */

        /* AOD timer expiry check (even when on other screens) */
        if (aod_active && aod_timer_mins > 0) {
            int64_t aod_elapsed = esp_timer_get_time() - aod_start_us;
            if (aod_elapsed >= (int64_t)aod_timer_mins * 60000000LL) {
                aod_active = false;
                ESP_LOGI(TAG, "AOD timer expired");
            }
        }

        /* Prevent sleep when power tools are active */
        bool prevent_sleep = aod_active ||
                             watch_scr == WSCR_TOOLS ||
                             watch_scr == WSCR_BLE_JAM ||
                             watch_scr == WSCR_BLE_SCAN ||
                             watch_scr == WSCR_TIMER;

        if (!oled_sleeping && !prevent_sleep &&
            (esp_timer_get_time() - last_activity_us) > (int64_t)effective_timeout_ms * 1000) {
            oled_sleeping = true;
            ssd1306_clear();
            ssd1306_update();
            ssd1306_display_on(false);
            ESP_LOGI(TAG, "OLED sleep");
        }

        if (oled_sleeping) {
            ble_service_loop();
            /* Longer tick interval while sleeping → big battery saving
             * Use delay_with_scan so button presses during sleep
             * are captured and queued for the next iteration.         */
            delay_with_scan(200);
            /* Check if find_watch was triggered while sleeping */
            if (find_watch_active) {
                oled_sleeping = false;
                ssd1306_display_on(true);
                ssd1306_set_contrast(brightness_val[brightness_level]);
            } else {
                continue;  /* skip drawing while asleep */
            }
        }

        /* ---- Find Watch OLED blink effect ---- */
        if (find_watch_active) {
            last_activity_us = esp_timer_get_time();
            find_watch_blinks++;
            bool on = (find_watch_blinks % 4) < 2;  /* 2 ticks on, 2 ticks off */
            ssd1306_display_on(on);
            if (on) {
                ssd1306_clear();
                ssd1306_invert(true);
                ssd1306_draw_string_centered(8,  "FIND ME!", SSD1306_WHITE, 2);
                ssd1306_draw_string_centered(32, ">> HERE <<", SSD1306_WHITE, 1);
                ssd1306_update();
            }
            if (find_watch_blinks >= 40) {  /* ~2 seconds (40 * 50ms) */
                find_watch_active = false;
                ssd1306_display_on(true);
                ssd1306_invert(false);
                ssd1306_set_contrast(brightness_val[brightness_level]);
                ESP_LOGI(TAG, "Find Watch: done");
            }
            /* Any button press stops it early */
            if (any_btn) {
                find_watch_active = false;
                ssd1306_display_on(true);
                ssd1306_invert(false);
                ssd1306_set_contrast(brightness_val[brightness_level]);
            }
            ble_service_loop();
            delay_with_scan(50);
            continue;
        }

        /* (mode toggle is now handled inside handle_watch / handle_hid
         *  via single BACK press on the main screen) */

        /* Chronos protocol housekeeping */
        ble_service_loop();

        /* For watch clock face: check if redraw is needed BEFORE clearing
         * framebuffer.  This avoids the blink from clear→skip→push-blank. */
        bool skip_draw = false;
        if (mode == MODE_WATCH && watch_scr == WSCR_TIME) {
            time_t now_t; time(&now_t);
            struct tm ti; localtime_r(&now_t, &ti);
            if (ti.tm_sec == last_drawn_sec &&
                ti.tm_min == last_drawn_min && !screen_dirty) {
                skip_draw = true;  /* keep previous framebuffer on display */
            }
        }

        if (!skip_draw) {
            ssd1306_clear();

            bool drew = true;
            if (mode == MODE_WATCH)
                drew = handle_watch(ev_up, ev_dn, ev_sel, ev_back);
            else
                handle_hid(ev_up, ev_dn, ev_sel, ev_back);

            if (drew) {
                /* status bar (yellow zone) */
                watch_face_draw_status(
                    mode == MODE_HID ? "HID" : "WATCH",
                    mode == MODE_HID ? ble_hid_is_connected() : ble_is_connected());

                ssd1306_update();
            }
        }

        /* Adaptive tick rate – slower when idle = longer light-sleep windows
         *   Clock face (no buttons):  2 Hz   → CPU sleeps ~490 ms/cycle
         *   HID list (no buttons):    5 Hz   → good for list browsing
         *   Active interaction:       20 Hz  → snappy UI
         *   Stopwatch running:        20 Hz  → centisecond precision  */
        int tick_ms;
        if (mode == MODE_WATCH &&
            (watch_scr == WSCR_TIME || watch_scr == WSCR_AOD) &&
            !any_btn && !sw_running)
            tick_ms = 500;   /* 2 Hz – clock face / AOD */
        else if (mode == MODE_WATCH &&
                 (watch_scr == WSCR_BLE_JAM || watch_scr == WSCR_BLE_SCAN))
            tick_ms = 200;   /* 5 Hz – jam/scan UI updates */
        else if (mode == MODE_HID && hid_scr == HSCR_LIST && !any_btn)
            tick_ms = 200;   /* 5 Hz – occasional list browsing */
        else
            tick_ms = 50;    /* 20 Hz – active use */
        delay_with_scan(tick_ms);
    }
}