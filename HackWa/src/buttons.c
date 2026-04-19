/*
 * GPIO Button Driver – 4 buttons with internal pull-ups
 * Debounce + long-press (1.5 s) detection
 *
 * Debounce: raw GPIO must be stable for DEBOUNCE_MS before the
 *           debounced state changes.  This filters mechanical
 *           switch bounce and prevents missed / double events.
 */
#include "buttons.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

#define DEBOUNCE_MS     20
#define LONG_PRESS_MS   1500

static const gpio_num_t pin_map[BTN_COUNT] = {
    GPIO_NUM_1,    /* UP     */
    GPIO_NUM_0,    /* DOWN   */
    GPIO_NUM_2,    /* SELECT */
    GPIO_NUM_21,   /* BACK   */
};

typedef struct {
    bool    pressed;        /* current debounced state        */
    bool    raw;            /* last raw GPIO reading           */
    int64_t raw_change;     /* ms timestamp of last raw change */
    bool    long_fired;     /* long-press already generated   */
    int64_t press_start;    /* timestamp (ms) of press edge   */
} btn_state_t;

static btn_state_t  state[BTN_COUNT];
static button_event_t events[BTN_COUNT];

/* ── Init ──────────────────────────────────────────────────── */
void buttons_init(void)
{
    memset(state,  0, sizeof(state));
    memset(events, 0, sizeof(events));

    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_config_t io = {
            .pin_bit_mask  = 1ULL << pin_map[i],
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_ENABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }
}

/* ── Scan & generate events (call frequently, ≥ 100 Hz) ───── */
void buttons_update(void)
{
    int64_t now = esp_timer_get_time() / 1000;   /* µs → ms */

    for (int i = 0; i < BTN_COUNT; i++) {
        bool raw = (gpio_get_level(pin_map[i]) == 0);  /* active-low */

        /* Track raw state changes for debounce */
        if (raw != state[i].raw) {
            state[i].raw        = raw;
            state[i].raw_change = now;
        }

        /* Accept new debounced state only after stable for DEBOUNCE_MS */
        if (state[i].raw != state[i].pressed &&
            (now - state[i].raw_change) >= DEBOUNCE_MS) {

            if (state[i].raw) {
                /* rising edge (just pressed) */
                state[i].pressed     = true;
                state[i].press_start = now;
                state[i].long_fired  = false;
            } else {
                /* falling edge (just released) → short-press event */
                state[i].pressed = false;
                if (!state[i].long_fired) {
                    events[i] = BTN_EVT_PRESS;
                }
            }
        }

        /* held long enough → long-press event */
        if (state[i].pressed && !state[i].long_fired) {
            if (now - state[i].press_start >= LONG_PRESS_MS) {
                state[i].long_fired = true;
                events[i] = BTN_EVT_LONG_PRESS;
            }
        }
    }
}

/* ── Consume one-shot event ────────────────────────────────── */
button_event_t buttons_get_event(button_id_t b)
{
    if (b >= BTN_COUNT) return BTN_EVT_NONE;
    button_event_t e = events[b];
    events[b] = BTN_EVT_NONE;
    return e;
}

bool buttons_is_pressed(button_id_t b)
{
    return (b < BTN_COUNT) && state[b].pressed;
}
