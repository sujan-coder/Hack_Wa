#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

/* Button IDs – match physical wiring */
typedef enum {
    BTN_UP = 0,     /* D0 / GPIO0  */
    BTN_DOWN,       /* D1 / GPIO1  */
    BTN_SELECT,     /* D2 / GPIO2  */
    BTN_BACK,       /* D3 / GPIO21 */
    BTN_COUNT
} button_id_t;

/* One-shot event types */
typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_PRESS,          /* short press (on release) */
    BTN_EVT_LONG_PRESS,     /* held ≥ 2 s               */
} button_event_t;

void           buttons_init(void);
void           buttons_update(void);           /* call once per loop tick */
button_event_t buttons_get_event(button_id_t b); /* consumes the event    */
bool           buttons_is_pressed(button_id_t b);

#endif /* BUTTONS_H */
