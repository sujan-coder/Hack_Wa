#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/* ── Lifecycle ─────────────────────────────────────────────── */
void ble_service_init(void);             /* starts in watch mode            */
void ble_service_loop(void);             /* call from main loop (~20 Hz)    */
void ble_switch_watch_mode(void);        /* disconnect + re-advertise watch  */
void ble_switch_hid_mode(void);          /* disconnect + re-advertise HID    */

/* ── State queries ─────────────────────────────────────────────────── */
bool ble_is_connected(void);             /* watch-mode connection           */
bool ble_hid_is_connected(void);         /* HID-mode connection             */

/* ── Notifications (populated via BLE) ─────────────────────── */
#define NOTIF_TITLE_LEN 32
#define NOTIF_BODY_LEN  64
#define MAX_NOTIFS       5

int  ble_get_notif_count(void);
void ble_get_notif(int idx, char *title, int tlen, char *body, int blen);
void notif_count_clear(void);            /* clear all notifications         */
bool ble_has_new_notif(void);            /* true if notif arrived (auto-clears) */
/* ── HID keyboard output ──────────────────────────────────────────── */
void ble_hid_type_string(const char *str);
void ble_hid_send_enter(void);
/* ── Find My Phone ─────────────────────────────────────────── */
void ble_send_find_phone(void);          /* sends AT+FP via NUS TX          *//* ── Mode switch notification ──────────────────────────── */
void ble_send_mode_notify(const char *mode_str); /* AT+MD=HID/WATCH      */

/* ── BLE Jam (advertising flood) ───────────────────────────── */
void     ble_start_jam(void);
void     ble_stop_jam(void);
bool     ble_jam_is_active(void);
uint32_t ble_jam_get_pkt_count(void);

/* ── BLE Scanner (passive discovery) ──────────────────────── */
#define BLE_SCAN_MAX_RESULTS 20

typedef struct {
    uint8_t addr[6];
    int8_t  rssi;
    char    name[16];
} ble_scan_result_t;

void ble_start_scan(void);
void ble_stop_scan(void);
bool ble_scan_is_active(void);
int  ble_scan_get_count(void);
const ble_scan_result_t *ble_scan_get_results(void);

#endif /* BLE_SERVICE_H */
