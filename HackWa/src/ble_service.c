/*
 * BLE Service – Watch mode + HID Keyboard mode
 *
 * Watch mode (CTS + ANS + NUS + BAS + DIS):
 *   - Current Time Service (0x1805)   →  time sync (read/write/notify)
 *   - Alert Notification Service (0x1811) →  phone notifications
 *   - Device Information Service (0x180A)
 *   - Battery Service (0x180F)
 *
 * HID mode (BLE HID Keyboard):
 *   - Human Interface Device Service (0x1812)
 *   - Types passwords as BLE keyboard keystrokes
 *
 * NUS AT-command protocol (companion app / BLE UART):
 *   AT+DT=YYYYMMDDHHmmss       – set date/time
 *   AT+NT=title|body            – push notification
 *   AT+PS=slot|label|password   – store password
 *   AT+PD=slot                  – delete password
 *   AT+TZ=TZ_string             – set POSIX timezone
 */
#include "ble_service.h"
#include "pw_store.h"
#include "hid_codes.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* NVS-based bond storage (no public header in ESP-IDF NimBLE) */
void ble_store_config_init(void);

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_random.h"

#define TAG "BLE"

/* ═══════════════════════════════════════════════════════════════
 *  State
 * ═══════════════════════════════════════════════════════════════*/
static uint8_t  ble_addr_type;           /* current advertising address type */
static uint8_t  watch_addr_type;         /* saved public addr type (for phone) */
static uint8_t  hid_rnd_addr[6];         /* derived random static addr (for laptop) */
static bool     hid_addr_ready = false;

static uint16_t cur_conn      = BLE_HS_CONN_HANDLE_NONE;
static bool     is_connected  = false;
static bool     ble_shutting_down = false;

/* HID mode state */
static bool     hid_mode      = false;   /* false=watch, true=HID keyboard */
static uint16_t hid_input_handle;        /* HID Input Report char handle   */
static uint8_t  hid_report[8] = {0};     /* modifier, reserved, keys[6]    */
static uint8_t  proto_mode    = 1;       /* 0=boot, 1=report               */

/* Notification ring buffer */
typedef struct { char title[NOTIF_TITLE_LEN]; char body[NOTIF_BODY_LEN]; } notif_t;
static notif_t notifs[MAX_NOTIFS];
static int     notif_count = 0;

/* Attribute handles filled by NimBLE */
static uint16_t nus_tx_handle;
static uint16_t cts_handle;
static uint16_t ans_new_alert_handle;

/* Chronos protocol state */
static bool      chronos_info_pending = false;
static TickType_t chronos_info_tick   = 0;

/* ═══════════════════════════════════════════════════════════════
 *  NUS UUIDs (Nordic UART Service)
 * ═══════════════════════════════════════════════════════════════*/
static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* ═══════════════════════════════════════════════════════════════
 *  Notification insert helper (shared by ANS + NUS)
 * ═══════════════════════════════════════════════════════════════*/
static bool new_notif_flag = false;

static void push_notification(const char *title, const char *body)
{
    if (notif_count < MAX_NOTIFS) notif_count++;
    for (int i = notif_count - 1; i > 0; i--)
        notifs[i] = notifs[i - 1];
    strncpy(notifs[0].title, title, NOTIF_TITLE_LEN - 1);
    notifs[0].title[NOTIF_TITLE_LEN - 1] = '\0';
    strncpy(notifs[0].body,  body,  NOTIF_BODY_LEN - 1);
    notifs[0].body[NOTIF_BODY_LEN - 1] = '\0';
    new_notif_flag = true;
    ESP_LOGI(TAG, "Notification: %s", notifs[0].title);
}

/* ═══════════════════════════════════════════════════════════════
 *  Chronos protocol helpers – send via NUS TX
 * ═══════════════════════════════════════════════════════════════*/
static void chronos_send(const uint8_t *data, uint16_t len)
{
    if (cur_conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) {
        int rc = ble_gatts_notify_custom(cur_conn, nus_tx_handle, om);
        if (rc != 0) ESP_LOGW(TAG, "chronos_send rc=%d", rc);
    }
}

/* ── Find My Phone – send AT+FP to connected app ── */
void ble_send_find_phone(void)
{
    const char *cmd = "AT+FP";
    const uint8_t *data = (const uint8_t *)cmd;
    uint16_t len = strlen(cmd);
    if (cur_conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) {
        int rc = ble_gatts_notify_custom(cur_conn, nus_tx_handle, om);
        if (rc != 0) ESP_LOGW(TAG, "find_phone send rc=%d", rc);
        else ESP_LOGI(TAG, "Find My Phone sent");
    }
}

/* ── Notify companion of mode switch (send before disconnect) ── */
void ble_send_mode_notify(const char *mode_str)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+MD=%s", mode_str);
    if (cur_conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *)cmd, strlen(cmd));
    if (om) {
        int rc = ble_gatts_notify_custom(cur_conn, nus_tx_handle, om);
        if (rc != 0) ESP_LOGW(TAG, "mode_notify send rc=%d", rc);
        else ESP_LOGI(TAG, "Mode notify sent: %s", mode_str);
        /* Small delay to let the notification reach the phone before disconnect */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void chronos_send_info(void)
{
    uint8_t cmd[] = {
        0xAB, 0x00, 0x11, 0xFF, 0x92, 0xC0,
        0x01, 90,
        0x00, 0xFB, 0x1E, 0x40, 0xC0, 0x0E, 0x32, 0x28,
        0x00, 0xE2,
        0x81,
        0x80
    };
    chronos_send(cmd, 20);
    ESP_LOGI(TAG, "Chronos: sent device info");
}

static void chronos_send_battery(void)
{
    uint8_t cmd[] = {0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80, 0x00, 100};
    chronos_send(cmd, 8);
    ESP_LOGI(TAG, "Chronos: sent battery");
}

/* ═══════════════════════════════════════════════════════════════
 *  Chronos binary protocol parser (0xAB commands)
 * ═══════════════════════════════════════════════════════════════*/
static void chronos_process(const uint8_t *data, uint16_t len)
{
    if (data[0] != 0xAB && data[0] != 0xEA) return;

    ESP_LOGI(TAG, "Chronos cmd: 0x%02X sub=0x%02X len=%d",
             data[4], (len > 5 ? data[5] : 0), len);

    if (data[0] == 0xAB) {
        switch (data[4]) {

        case 0x93: /* Time set */
            if (len >= 14) {
                struct tm tm = {0};
                tm.tm_year = (data[7] * 256 + data[8]) - 1900;
                tm.tm_mon  = data[9] - 1;
                tm.tm_mday = data[10];
                tm.tm_hour = data[11];
                tm.tm_min  = data[12];
                tm.tm_sec  = data[13];
                struct timeval tv = { .tv_sec = mktime(&tm) };
                settimeofday(&tv, NULL);
                extern void force_screen_redraw(void);
                force_screen_redraw();
                ESP_LOGI(TAG, "Chronos time: %04d-%02d-%02d %02d:%02d:%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
            }
            break;

        case 0x72: { /* Notification */
            if (len >= 8) {
                int icon  = data[6];
                int state = data[7];
                if (state == 0x02 && len > 8) {
                    char msg[128] = {0};
                    int mlen = (int)(len - 8);
                    if (mlen > 127) mlen = 127;
                    memcpy(msg, &data[8], mlen);
                    msg[mlen] = '\0';

                    char *nl = strchr(msg, '\n');
                    if (nl) {
                        *nl = '\0';
                        push_notification(msg, nl + 1);
                    } else {
                        const char *app = "Notification";
                        switch (icon) {
                            case 0x03: app = "SMS";       break;
                            case 0x04: app = "Email";     break;
                            case 0x07: app = "WhatsApp";  break;
                            case 0x0A: app = "Facebook";  break;
                            case 0x0B: app = "Twitter";   break;
                            case 0x0E: app = "Instagram"; break;
                            case 0x0F: app = "Telegram";  break;
                        }
                        push_notification(app, msg);
                    }
                }
            }
            break;
        }

        case 0x7C: /* 24h mode */
            ESP_LOGI(TAG, "24h mode: %d", (len > 6) ? data[6] : -1);
            break;

        case 0x71: /* Find device */
            ESP_LOGI(TAG, "Find device request");
            break;

        case 0x23: /* Reset */
            ESP_LOGI(TAG, "Reset request from Chronos");
            break;

        case 0x91: /* Phone battery info */
            if (data[3] == 0xFE && len >= 8)
                ESP_LOGI(TAG, "Phone bat: %d%% chg=%d", data[7], data[6]);
            break;

        case 0x20: /* Sync complete */
            if (data[3] == 0xFE)
                ESP_LOGI(TAG, "Chronos sync complete");
            break;

        default:
            ESP_LOGI(TAG, "Unknown 0xAB cmd: 0x%02X", data[4]);
            break;
        }
    }

    if (data[0] == 0xEA) {
        switch (data[4]) {
        case 0x91: /* Device info request */
            ESP_LOGI(TAG, "Chronos info request (0xEA)");
            chronos_send_info();
            vTaskDelay(pdMS_TO_TICKS(200));
            chronos_send_battery();
            break;
        default:
            ESP_LOGI(TAG, "Unknown 0xEA cmd: 0x%02X", data[4]);
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  NUS command parser (Chronos binary + AT-text fallback)
 * ═══════════════════════════════════════════════════════════════*/
static void nus_process(const uint8_t *data, uint16_t len)
{
    /* Chronos binary protocol (0xAB / 0xEA prefix) */
    if (len >= 4 && (data[0] == 0xAB || data[0] == 0xEA) &&
        (data[3] == 0xFF || data[3] == 0xFE)) {
        chronos_process(data, len);
        return;
    }

    /* AT-command fallback (text, for BLE UART apps) */
    char buf[256];
    int n = (len < 255) ? len : 255;
    memcpy(buf, data, n);
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == '\n')) buf[--n] = '\0';

    ESP_LOGI(TAG, "NUS RX: %s", buf);

    if (strncmp(buf, "AT+DT=", 6) == 0 && n >= 20) {
        struct tm tm = {0};
        sscanf(buf + 6, "%4d%2d%2d%2d%2d%2d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        tm.tm_year -= 1900; tm.tm_mon -= 1;
        struct timeval tv = { .tv_sec = mktime(&tm), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        extern void force_screen_redraw(void);
        force_screen_redraw();
        ESP_LOGI(TAG, "Time set OK");
        return;
    }
    if (strncmp(buf, "AT+NT=", 6) == 0) {
        char *sep = strchr(buf + 6, '|');
        if (sep) { *sep = '\0'; push_notification(buf + 6, sep + 1); }
        return;
    }
    if (strncmp(buf, "AT+PS=", 6) == 0) {
        char *p = buf + 6;
        char *s1 = strchr(p, '|');
        if (s1) {
            *s1 = '\0'; int slot = atoi(p);
            char *s2 = strchr(s1 + 1, '|');
            if (s2) { *s2 = '\0'; pw_store_set(slot, s1 + 1, s2 + 1); }
        }
        return;
    }
    if (strncmp(buf, "AT+PD=", 6) == 0) { pw_store_delete(atoi(buf + 6)); return; }
    if (strncmp(buf, "AT+TZ=", 6) == 0) {
        setenv("TZ", buf + 6, 1); tzset();
        ESP_LOGI(TAG, "TZ set: %s", buf + 6);
        return;
    }
    if (strncmp(buf, "AT+FW", 5) == 0) {
        ESP_LOGI(TAG, "Find Watch triggered from app");
        extern void trigger_find_watch(void);
        trigger_find_watch();
        return;
    }
    if (strncmp(buf, "AT+ST=", 6) == 0) {
        int val = atoi(buf + 6);
        if (val >= 5 && val <= 300) {
            extern void set_screen_timeout(int seconds);
            set_screen_timeout(val);
            ESP_LOGI(TAG, "Screen timeout set: %d s", val);
        }
        return;
    }
    if (strncmp(buf, "AT+BR=", 6) == 0) {
        int val = atoi(buf + 6);
        if (val >= 0 && val <= 2) {
            extern void set_brightness(int level);
            set_brightness(val);
            ESP_LOGI(TAG, "Brightness set: %d", val);
        }
        return;
    }
    ESP_LOGW(TAG, "Unknown NUS cmd");
}

/* ═══════════════════════════════════════════════════════════════
 *  GATT Access Callbacks
 * ═══════════════════════════════════════════════════════════════*/

/* ---- Current Time Service (0x1805 / 0x2A2B) ---- */
static int cts_access(uint16_t ch, uint16_t ah,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t d[10];
        uint16_t olen = 0;
        ble_hs_mbuf_to_flat(ctxt->om, d, sizeof(d), &olen);
        if (olen >= 7) {
            struct tm tm = {0};
            tm.tm_year = (d[1] << 8 | d[0]) - 1900;
            tm.tm_mon  = d[2] - 1;
            tm.tm_mday = d[3];
            tm.tm_hour = d[4];
            tm.tm_min  = d[5];
            tm.tm_sec  = d[6];
            struct timeval tv = { .tv_sec = mktime(&tm) };
            settimeofday(&tv, NULL);
            extern void force_screen_redraw(void);
            force_screen_redraw();
            ESP_LOGI(TAG, "CTS time written");
        }
        return 0;
    }
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    uint16_t yr = t.tm_year + 1900;
    uint8_t b[10] = {
        (uint8_t)(yr & 0xFF), (uint8_t)(yr >> 8),
        (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday,
        (uint8_t)t.tm_hour, (uint8_t)t.tm_min, (uint8_t)t.tm_sec,
        (uint8_t)t.tm_wday, 0, 0
    };
    os_mbuf_append(ctxt->om, b, 10);
    return 0;
}

/* ---- NUS RX (phone writes to us) ---- */
static int nus_rx_access(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t buf[256];
    uint16_t len = 0;
    ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
    nus_process(buf, len);
    return 0;
}

/* ---- NUS TX (we notify phone) ---- */
static int nus_tx_access(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* ---- Battery Service ---- */
static int bas_access(uint16_t ch, uint16_t ah,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t lvl = 100;
    os_mbuf_append(ctxt->om, &lvl, 1);
    return 0;
}

/* ---- Device Information Service ---- */
static int dis_access(uint16_t ch, uint16_t ah,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *val = (const char *)arg;
    os_mbuf_append(ctxt->om, val, strlen(val));
    return 0;
}

static const uint8_t pnp_id_val[7] = {
    0x02, 0xE5, 0x02, 0x01, 0xA1, 0x01, 0x00
};

static int pnp_id_access(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, pnp_id_val, sizeof(pnp_id_val));
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Alert Notification Service (ANS, 0x1811) – Chronos compat
 * ═══════════════════════════════════════════════════════════════*/
static int ans_sup_new_alert_access(uint16_t ch, uint16_t ah,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t cat[2] = {0xFF, 0x03};
    os_mbuf_append(ctxt->om, cat, 2);
    return 0;
}

static int ans_new_alert_access(uint16_t ch, uint16_t ah,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[256];
        uint16_t len = 0;
        ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
        buf[len] = '\0';
        if (len >= 3) {
            char *text = (char *)&buf[2];
            char title[NOTIF_TITLE_LEN];
            char body[NOTIF_BODY_LEN];
            char *nl = strchr(text, '\n');
            if (nl) {
                int tl = nl - text;
                if (tl >= NOTIF_TITLE_LEN) tl = NOTIF_TITLE_LEN - 1;
                memcpy(title, text, tl);
                title[tl] = '\0';
                strncpy(body, nl + 1, NOTIF_BODY_LEN - 1);
                body[NOTIF_BODY_LEN - 1] = '\0';
            } else {
                strncpy(title, text, NOTIF_TITLE_LEN - 1);
                title[NOTIF_TITLE_LEN - 1] = '\0';
                body[0] = '\0';
            }
            push_notification(title, body);
        }
        return 0;
    }
    return 0;
}

static int ans_sup_unread_access(uint16_t ch, uint16_t ah,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t cat[2] = {0xFF, 0x03};
    os_mbuf_append(ctxt->om, cat, 2);
    return 0;
}

static int ans_unread_access(uint16_t ch, uint16_t ah,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t d[2] = {0x00, (uint8_t)notif_count};
    os_mbuf_append(ctxt->om, d, 2);
    return 0;
}

static int ans_cp_access(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t cmd[2] = {0};
    uint16_t len = 0;
    ble_hs_mbuf_to_flat(ctxt->om, cmd, 2, &len);
    ESP_LOGI(TAG, "ANS CP: cmd=%d cat=%d", cmd[0], cmd[1]);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  HID Report Map (standard 101-key keyboard)
 * ═══════════════════════════════════════════════════════════════*/
static const uint8_t hid_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Minimum (224) – Left Control
    0x29, 0xE7,       //   Usage Maximum (231) – Right GUI
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute) – modifier byte
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant) – reserved byte
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array)
    0xC0              // End Collection
};

static const uint8_t hid_info_val[4] = {0x11, 0x01, 0x00, 0x02}; // v1.11, not localized, normally connectable

/* ── HID GATT Access Callbacks ─────────────────────────────── */
static int hid_rmap_access(uint16_t ch, uint16_t ah,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
    return 0;
}

static int hid_info_access(uint16_t ch, uint16_t ah,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, hid_info_val, sizeof(hid_info_val));
    return 0;
}

static int hid_input_access(uint16_t ch, uint16_t ah,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, hid_report, sizeof(hid_report));
    return 0;
}

static int hid_rref_access(uint16_t ch, uint16_t ah,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Report Reference: Report ID = 1, Type = Input (1) */
    uint8_t ref[2] = {0x01, 0x01};
    os_mbuf_append(ctxt->om, ref, 2);
    return 0;
}

static int hid_proto_access(uint16_t ch, uint16_t ah,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t val = 0;
        uint16_t len = 0;
        ble_hs_mbuf_to_flat(ctxt->om, &val, 1, &len);
        proto_mode = val;
        return 0;
    }
    os_mbuf_append(ctxt->om, &proto_mode, 1);
    return 0;
}

static int hid_cp_access(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t cmd = 0;
    uint16_t len = 0;
    ble_hs_mbuf_to_flat(ctxt->om, &cmd, 1, &len);
    ESP_LOGI(TAG, "HID Control Point: %d", cmd);
    return 0;
}

static int boot_kbd_input_access(uint16_t ch, uint16_t ah,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, hid_report, sizeof(hid_report));
    return 0;
}

static int boot_kbd_output_access(uint16_t ch, uint16_t ah,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  GATT Service Table – Watch mode (CTS + ANS + NUS + BAS + DIS)
 * ═══════════════════════════════════════════════════════════════*/
static const struct ble_gatt_svc_def gatt_svcs_watch[] = {

    /* ----- Current Time Service 0x1805 ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1805),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = BLE_UUID16_DECLARE(0x2A2B),
                .access_cb  = cts_access,
                .val_handle = &cts_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
                             | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    /* ----- Nordic UART Service ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (const ble_uuid_t *)&nus_svc_uuid,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = (const ble_uuid_t *)&nus_rx_uuid,
                .access_cb = nus_rx_access,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid       = (const ble_uuid_t *)&nus_tx_uuid,
                .access_cb  = nus_tx_access,
                .val_handle = &nus_tx_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    /* ----- Alert Notification Service 0x1811 ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1811),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A47),
                .access_cb = ans_sup_new_alert_access,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid       = BLE_UUID16_DECLARE(0x2A46),
                .access_cb  = ans_new_alert_access,
                .val_handle = &ans_new_alert_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A48),
                .access_cb = ans_sup_unread_access,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A45),
                .access_cb = ans_unread_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A44),
                .access_cb = ans_cp_access,
                .flags     = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },

    /* ----- Battery Service 0x180F ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = bas_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    /* ----- Device Information Service 0x180A ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A29),
                .access_cb = dis_access,
                .arg       = (void *)"HackWa",
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A24),
                .access_cb = dis_access,
                .arg       = (void *)"HackWa-V1",
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A50),
                .access_cb = pnp_id_access,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },

    { 0 }
};

/* ═══════════════════════════════════════════════════════════════
 *  GATT Service Table – HID mode (HID + BAS + DIS ONLY)
 *  Real keyboards expose only these 3 services.
 * ═══════════════════════════════════════════════════════════════*/
static const struct ble_gatt_svc_def gatt_svcs_hid[] = {

    /* ----- Battery Service 0x180F ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = bas_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    /* ----- Device Information Service 0x180A ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A29),
                .access_cb = dis_access,
                .arg       = (void *)"HackWa",
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A24),
                .access_cb = dis_access,
                .arg       = (void *)"HackWa-V1",
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid      = BLE_UUID16_DECLARE(0x2A50),
                .access_cb = pnp_id_access,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },

    /* ----- HID Service 0x1812 ----- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { /* Report Map */
                .uuid      = BLE_UUID16_DECLARE(0x2A4B),
                .access_cb = hid_rmap_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            { /* HID Information */
                .uuid      = BLE_UUID16_DECLARE(0x2A4A),
                .access_cb = hid_info_access,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            { /* HID Input Report (notify to type keys) */
                .uuid       = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb  = hid_input_access,
                .val_handle = &hid_input_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
                             | BLE_GATT_CHR_F_NOTIFY,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid      = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_rref_access,
                    },
                    { 0 }
                },
            },
            { /* Protocol Mode */
                .uuid      = BLE_UUID16_DECLARE(0x2A4E),
                .access_cb = hid_proto_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { /* HID Control Point */
                .uuid      = BLE_UUID16_DECLARE(0x2A4C),
                .access_cb = hid_cp_access,
                .flags     = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { /* Boot Keyboard Input Report */
                .uuid      = BLE_UUID16_DECLARE(0x2A22),
                .access_cb = boot_kbd_input_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { /* Boot Keyboard Output Report */
                .uuid      = BLE_UUID16_DECLARE(0x2A32),
                .access_cb = boot_kbd_output_access,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP
                            | BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },

    { 0 }
};

/* ═══════════════════════════════════════════════════════════════
 *  GAP Event Handler
 * ═══════════════════════════════════════════════════════════════*/
static void start_advertising(void);

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d handle=%d",
                 ev->connect.status, ev->connect.conn_handle);
        if (ev->connect.status == 0) {
            cur_conn     = ev->connect.conn_handle;
            is_connected = true;
            ESP_LOGI(TAG, "Connected (%s mode)", hid_mode ? "HID" : "Watch");

            /* Request connection params optimised per mode:
             *   HID:   15–30 ms, latency 4 → battery-friendly but responsive for typing
             *   Watch: 30–60 ms, latency 10 → phone link needs very little bandwidth */
            {
                struct ble_gap_upd_params params = {
                    .itvl_min  = hid_mode ? 0x000C : 0x0018,  /* 15/30 ms  */
                    .itvl_max  = hid_mode ? 0x0018 : 0x0030,  /* 30/60 ms  */
                    .latency   = hid_mode ? 4      : 10,      /* skip idle intervals */
                    .supervision_timeout = 400, /* 4 s */
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                ble_gap_update_params(cur_conn, &params);
            }
            /* HID requires encryption – initiate pairing */
            if (hid_mode) {
                ble_gap_security_initiate(cur_conn);
            }
        } else {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=0x%02x", ev->disconnect.reason);
        cur_conn     = BLE_HS_CONN_HANDLE_NONE;
        is_connected = false;
        if (!ble_shutting_down) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!ble_shutting_down) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption change status=%d", ev->enc_change.status);
        if (ev->enc_change.status == 0) {
            ESP_LOGI(TAG, "Encryption OK");
        } else {
            /* Don't terminate – just log. Host may retry on its own. */
            ESP_LOGW(TAG, "Encryption failed (status=%d)", ev->enc_change.status);
        }
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "conn params updated status=%d", ev->conn_update.status);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* Just Works – no passkey/pin needed (like real BLE keyboards) */
        if (ev->passkey.params.action == BLE_SM_IOACT_NONE ||
            ev->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io pk = { .action = ev->passkey.params.action };
            pk.numcmp_accept = 1;
            ble_sm_inject_io(ev->passkey.conn_handle, &pk);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe handle=%d cur_notify=%d",
                 ev->subscribe.attr_handle, ev->subscribe.cur_notify);
        if (ev->subscribe.attr_handle == nus_tx_handle &&
            ev->subscribe.cur_notify) {
            chronos_info_pending = true;
            chronos_info_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "HackWa subscribed – will send info");
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: %d", ev->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Advertising – adapts name & appearance for watch vs HID mode
 * ═══════════════════════════════════════════════════════════════*/
static void start_advertising(void)
{
    int rc;
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    if (hid_mode) {
        /* ── HID keyboard advertising ── */
        static const char hid_name[] = "Keyway-1.0";
        fields.name             = (uint8_t *)hid_name;
        fields.name_len         = sizeof(hid_name) - 1;
        fields.name_is_complete = 1;

        static ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
        fields.uuids16             = &hid_uuid;
        fields.num_uuids16         = 1;
        fields.uuids16_is_complete = 1;

        ble_svc_gap_device_name_set("Keyway-1.0");
    } else {
        /* ── Watch / Chronos advertising ── */
        static const char watch_name[] = "HackWa";
        fields.name             = (uint8_t *)watch_name;
        fields.name_len         = sizeof(watch_name) - 1;
        fields.name_is_complete = 1;

        static ble_uuid128_t adv_nus = BLE_UUID128_INIT(
            0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
            0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
        fields.uuids128             = &adv_nus;
        fields.num_uuids128         = 1;
        fields.uuids128_is_complete = 1;

        ble_svc_gap_device_name_set("HackWa");
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields fail: %d", rc);
    }

    struct ble_hs_adv_fields rsp = {0};
    rsp.tx_pwr_lvl_is_present = 1;
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rsp.appearance            = hid_mode ? 0x03C1 : 0x00C1; /* Keyboard : Watch */
    rsp.appearance_is_present = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields fail: %d", rc);
    }

    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    /* HID: slower advertising (100–150 ms) to save battery vs watch (30–60 ms) */
    adv.itvl_min  = hid_mode ? 0x00A0 : 0x0030;  /* HID 100ms : Watch 30ms */
    adv.itvl_max  = hid_mode ? 0x00F0 : 0x0060;  /* HID 150ms : Watch 60ms */

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER,
                               &adv, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start fail: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as %s", hid_mode ? "Keyway-1.0" : "HackWa");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  NimBLE callbacks
 * ═══════════════════════════════════════════════════════════════*/
static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced");
    ble_hs_id_infer_auto(0, &ble_addr_type);
    watch_addr_type = ble_addr_type;   /* remember for watch mode */

    /* Derive a unique random static address for HID mode from
     * the chip's public address so the laptop sees a different
     * identity than the phone.  Random-static = top 2 bits = 11. */
    if (!hid_addr_ready) {
        uint8_t pub[6];
        ble_hs_id_copy_addr(ble_addr_type, pub, NULL);
        for (int i = 0; i < 6; i++)
            hid_rnd_addr[i] = pub[i] ^ 0xAA;   /* deterministic offset */
        hid_rnd_addr[5] |= 0xC0;               /* mark as random static */
        hid_rnd_addr[5] &= ~0x01;              /* ensure not identical to pub */
        hid_addr_ready = true;
        ESP_LOGI(TAG, "HID addr: %02X:%02X:%02X:%02X:%02X:%02X",
                 hid_rnd_addr[5], hid_rnd_addr[4], hid_rnd_addr[3],
                 hid_rnd_addr[2], hid_rnd_addr[1], hid_rnd_addr[0]);
    }

    ble_shutting_down = false;
    start_advertising();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ═══════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════*/
void ble_service_init(void)
{
    ESP_LOGI(TAG, "BLE init start");
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;

    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    /* ── Initialise NVS-based bond storage (CRITICAL for pairing) ── */
    ble_store_config_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs_watch);
    ble_gatts_add_svcs(gatt_svcs_watch);
    ble_svc_gap_device_name_set("HackWa");

    hid_mode = false;
    ble_shutting_down = false;
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE initialised (watch mode)");
}

void ble_service_loop(void)
{
    if (chronos_info_pending && is_connected) {
        if ((xTaskGetTickCount() - chronos_info_tick) > pdMS_TO_TICKS(3000)) {
            chronos_info_pending = false;
            chronos_send_info();
            vTaskDelay(pdMS_TO_TICKS(200));
            chronos_send_battery();
        }
    }
}

bool ble_is_connected(void) { return is_connected && !hid_mode; }
bool ble_hid_is_connected(void) { return is_connected && hid_mode; }

int ble_get_notif_count(void) { return notif_count; }

void notif_count_clear(void)
{
    notif_count = 0;
    memset(notifs, 0, sizeof(notifs));
    ESP_LOGI(TAG, "Notifications cleared");
}

bool ble_has_new_notif(void)
{
    bool f = new_notif_flag;
    new_notif_flag = false;
    return f;
}

void ble_get_notif(int idx, char *title, int tlen, char *body, int blen)
{
    if (idx < 0 || idx >= notif_count) {
        if (tlen > 0) title[0] = '\0';
        if (blen > 0) body[0]  = '\0';
        return;
    }
    strncpy(title, notifs[idx].title, tlen - 1);  title[tlen - 1] = '\0';
    strncpy(body,  notifs[idx].body,  blen - 1);  body[blen - 1]  = '\0';
}

/* ═══════════════════════════════════════════════════════════════
 *  Mode Switching – disconnect + re-advertise with new identity
 * ═══════════════════════════════════════════════════════════════*/
static void ble_full_stop(void)
{
    ble_shutting_down = true;
    if (is_connected) {
        ble_gap_terminate(cur_conn, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ble_gap_adv_stop();
    cur_conn     = BLE_HS_CONN_HANDLE_NONE;
    is_connected = false;
}

/* Re-register GATT table (called during mode switch) */
static void register_gatt_table(const struct ble_gatt_svc_def *svcs)
{
    ble_gatts_reset();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(svcs);
    ble_gatts_add_svcs(svcs);
    ble_gatts_start();
}

void ble_switch_hid_mode(void)
{
    ble_full_stop();
    register_gatt_table(gatt_svcs_hid);
    ble_svc_gap_device_name_set("Keyway-1.0");
    hid_mode = true;

    /* Use the HID-specific random static address so the laptop
     * sees a completely different BLE identity than the phone. */
    ble_hs_id_set_rnd(hid_rnd_addr);
    ble_addr_type = BLE_OWN_ADDR_RANDOM;

    ble_shutting_down = false;
    start_advertising();
    ESP_LOGI(TAG, "Switched to HID mode (HID+BAS+DIS only, random addr)");
}

void ble_switch_watch_mode(void)
{
    ble_full_stop();
    register_gatt_table(gatt_svcs_watch);
    ble_svc_gap_device_name_set("HackWa");

    /* Restore the original public address for the phone */
    ble_addr_type = watch_addr_type;
    hid_mode = false;
    ble_shutting_down = false;
    start_advertising();
    ESP_LOGI(TAG, "Switched to Watch mode");
}

/* ═══════════════════════════════════════════════════════════════
 *  HID Keyboard Output – type string & send Enter
 *
 *  Mimics Teensy-style delays:
 *    - random 5–20 ms between each character
 *    - 5 ms key-hold time per key
 *    - 50 ms pause after full string
 * ═══════════════════════════════════════════════════════════════*/
static void send_report(void)
{
    if (cur_conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(hid_report, sizeof(hid_report));
    if (om) {
        ble_gatts_notify_custom(cur_conn, hid_input_handle, om);
    }
}

void ble_hid_type_string(const char *str)
{
    if (!is_connected || !hid_mode) return;

    for (const char *p = str; *p; p++) {
        uint8_t mod = 0, key = 0;
        if (!ascii_to_hid(*p, &mod, &key)) continue;

        /* Key down */
        memset(hid_report, 0, sizeof(hid_report));
        hid_report[0] = mod;
        hid_report[2] = key;
        send_report();
        vTaskDelay(pdMS_TO_TICKS(5));   /* 5 ms hold */

        /* Key up */
        memset(hid_report, 0, sizeof(hid_report));
        send_report();

        /* Random inter-key delay 5–20 ms (Teensy-style) */
        vTaskDelay(pdMS_TO_TICKS(5 + (esp_random() % 16)));
    }
    /* 50 ms pause after full password */
    vTaskDelay(pdMS_TO_TICKS(50));
}

void ble_hid_send_enter(void)
{
    if (!is_connected || !hid_mode) return;

    memset(hid_report, 0, sizeof(hid_report));
    hid_report[2] = HID_KEY_ENTER;
    send_report();
    vTaskDelay(pdMS_TO_TICKS(5));

    memset(hid_report, 0, sizeof(hid_report));
    send_report();
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* ═══════════════════════════════════════════════════════════════
 *  BLE Jam – Advertising Flood
 *
 *  Rapidly rotates random BLE addresses + advertising payloads
 *  on all 3 advertising channels (37, 38, 39), flooding the
 *  spectrum and disrupting nearby BLE scanning / connections.
 *  Intended for use with external power only.
 * ═══════════════════════════════════════════════════════════════*/
static bool         ble_jam_active = false;
static uint32_t     ble_jam_pkt_count = 0;
static TaskHandle_t ble_jam_task_handle = NULL;

static void ble_jam_task(void *param)
{
    ESP_LOGI(TAG, "BLE Jam task started");

    while (ble_jam_active) {
        /* Generate a fresh random static address each iteration */
        uint8_t rnd_addr[6];
        esp_fill_random(rnd_addr, 6);
        rnd_addr[5] |= 0xC0;      /* mark as random-static */
        rnd_addr[5] &= ~0x01;

        ble_gap_adv_stop();
        ble_hs_id_set_rnd(rnd_addr);

        /* Random advertising payload – printable device name */
        struct ble_hs_adv_fields fields = {0};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

        uint8_t rnd_name[10];
        esp_fill_random(rnd_name, sizeof(rnd_name));
        for (int i = 0; i < (int)sizeof(rnd_name); i++)
            rnd_name[i] = 'A' + (rnd_name[i] % 26);
        fields.name             = rnd_name;
        fields.name_len         = sizeof(rnd_name);
        fields.name_is_complete = 1;

        ble_gap_adv_set_fields(&fields);

        /* Non-connectable advertisement at fastest rate */
        struct ble_gap_adv_params adv = {0};
        adv.conn_mode = BLE_GAP_CONN_MODE_NON;
        adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
        adv.itvl_min  = 0x0020;   /* 20 ms */
        adv.itvl_max  = 0x0020;

        ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                          &adv, NULL, NULL);

        ble_jam_pkt_count++;
        vTaskDelay(pdMS_TO_TICKS(30));   /* rotate identity ~33 Hz */
    }

    ble_gap_adv_stop();
    ESP_LOGI(TAG, "BLE Jam stopped (%lu pkts)", (unsigned long)ble_jam_pkt_count);
    ble_jam_task_handle = NULL;
    vTaskDelete(NULL);
}

void ble_start_jam(void)
{
    if (ble_jam_active) return;
    ble_full_stop();

    ble_jam_active    = true;
    ble_jam_pkt_count = 0;
    xTaskCreate(ble_jam_task, "ble_jam", 4096, NULL, 5, &ble_jam_task_handle);
}

void ble_stop_jam(void)
{
    if (!ble_jam_active) return;
    ble_jam_active = false;
    vTaskDelay(pdMS_TO_TICKS(100));   /* wait for task to exit */

    /* Restore normal watch-mode BLE */
    if (hid_mode) {
        ble_hs_id_set_rnd(hid_rnd_addr);
        ble_addr_type = BLE_OWN_ADDR_RANDOM;
    } else {
        ble_addr_type = watch_addr_type;
    }
    ble_shutting_down = false;
    start_advertising();
}

bool     ble_jam_is_active(void)     { return ble_jam_active; }
uint32_t ble_jam_get_pkt_count(void) { return ble_jam_pkt_count; }

/* ═══════════════════════════════════════════════════════════════
 *  BLE Scanner – Passive Discovery
 *
 *  Performs a 30-second passive BLE scan and stores discovered
 *  device addresses, RSSI, and names (when available).
 * ═══════════════════════════════════════════════════════════════*/
static ble_scan_result_t scan_results[BLE_SCAN_MAX_RESULTS];
static int               scan_result_count = 0;
static bool              ble_scanning = false;

static int ble_scan_gap_event(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_DISC) {
        /* Update RSSI if already discovered */
        for (int i = 0; i < scan_result_count; i++) {
            if (memcmp(scan_results[i].addr, ev->disc.addr.val, 6) == 0) {
                scan_results[i].rssi = ev->disc.rssi;
                return 0;
            }
        }
        /* Add new device */
        if (scan_result_count < BLE_SCAN_MAX_RESULTS) {
            ble_scan_result_t *r = &scan_results[scan_result_count];
            memcpy(r->addr, ev->disc.addr.val, 6);
            r->rssi = ev->disc.rssi;

            /* Try to extract device name from advertising data */
            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, ev->disc.data,
                                         ev->disc.length_data) == 0 &&
                fields.name != NULL && fields.name_len > 0) {
                int nlen = fields.name_len < 15 ? (int)fields.name_len : 15;
                memcpy(r->name, fields.name, nlen);
                r->name[nlen] = '\0';
            } else {
                strcpy(r->name, "???");
            }
            scan_result_count++;
        }
        return 0;
    }

    if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGI(TAG, "Scan complete, %d devices", scan_result_count);
        ble_scanning = false;
    }
    return 0;
}

void ble_start_scan(void)
{
    if (ble_scanning || ble_jam_active) return;

    ble_full_stop();
    scan_result_count = 0;
    memset(scan_results, 0, sizeof(scan_results));
    ble_scanning = true;

    struct ble_gap_disc_params params = {0};
    params.passive           = 1;      /* listen only */
    params.itvl              = 0x0010; /* 10 ms interval */
    params.window            = 0x0010; /* 10 ms window (100 % duty) */
    params.filter_duplicates = 0;
    params.limited           = 0;

    int rc = ble_gap_disc(ble_addr_type, 30000 /* 30 s */,
                          &params, ble_scan_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        ble_scanning = false;
    } else {
        ESP_LOGI(TAG, "BLE scan started (30 s)");
    }
}

void ble_stop_scan(void)
{
    if (!ble_scanning) return;
    ble_gap_disc_cancel();
    ble_scanning = false;

    /* Restore normal advertising */
    if (hid_mode) {
        ble_hs_id_set_rnd(hid_rnd_addr);
        ble_addr_type = BLE_OWN_ADDR_RANDOM;
    } else {
        ble_addr_type = watch_addr_type;
    }
    ble_shutting_down = false;
    start_advertising();
}

bool ble_scan_is_active(void)                   { return ble_scanning; }
int  ble_scan_get_count(void)                   { return scan_result_count; }
const ble_scan_result_t *ble_scan_get_results(void) { return scan_results; }
