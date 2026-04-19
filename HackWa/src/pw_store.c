/*
 * Password Store – NVS-backed, up to 10 entries
 *
 * NVS keys:  "pw_L0" … "pw_L9"  (labels)
 *            "pw_P0" … "pw_P9"  (passwords)
 */
#include "pw_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define TAG       "PW_STORE"
#define NVS_NS    "hackwa_pw"

static nvs_handle_t nvs;

/* ── helpers ───────────────────────────────────────────────── */
static void key_label(int slot, char *k) { snprintf(k, 12, "pw_L%d", slot); }
static void key_pass (int slot, char *k) { snprintf(k, 12, "pw_P%d", slot); }

/* ── public API ────────────────────────────────────────────── */
void pw_store_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed (%s)", esp_err_to_name(err));
    }
}

int pw_store_count(void)
{
    int n = 0;
    char k[12];
    for (int i = 0; i < PW_MAX_SLOTS; i++) {
        key_label(i, k);
        size_t len = 0;
        if (nvs_get_str(nvs, k, NULL, &len) == ESP_OK && len > 1)
            n++;
    }
    return n;
}

bool pw_store_set(int slot, const char *label, const char *pass)
{
    if (slot < 0 || slot >= PW_MAX_SLOTS) return false;
    char k[12];

    key_label(slot, k);
    nvs_set_str(nvs, k, label);

    key_pass(slot, k);
    nvs_set_str(nvs, k, pass);

    nvs_commit(nvs);
    ESP_LOGI(TAG, "Slot %d set: %s", slot, label);
    return true;
}

bool pw_store_delete(int slot)
{
    if (slot < 0 || slot >= PW_MAX_SLOTS) return false;
    char k[12];

    key_label(slot, k);  nvs_erase_key(nvs, k);
    key_pass (slot, k);  nvs_erase_key(nvs, k);
    nvs_commit(nvs);
    ESP_LOGI(TAG, "Slot %d deleted", slot);
    return true;
}

bool pw_store_get_label(int slot, char *out, int maxlen)
{
    if (slot < 0 || slot >= PW_MAX_SLOTS) return false;
    char k[12];
    key_label(slot, k);
    size_t len = maxlen;
    if (nvs_get_str(nvs, k, out, &len) == ESP_OK)
        return true;
    out[0] = '\0';
    return false;
}

bool pw_store_get_pass(int slot, char *out, int maxlen)
{
    if (slot < 0 || slot >= PW_MAX_SLOTS) return false;
    char k[12];
    key_pass(slot, k);
    size_t len = maxlen;
    if (nvs_get_str(nvs, k, out, &len) == ESP_OK)
        return true;
    out[0] = '\0';
    return false;
}
