#ifndef PW_STORE_H
#define PW_STORE_H

#include <stdint.h>
#include <stdbool.h>

#define PW_MAX_SLOTS   10
#define PW_MAX_LABEL   21     /* 20 chars + NUL */
#define PW_MAX_PASS    33     /* 32 chars + NUL */

void pw_store_init(void);
int  pw_store_count(void);                                    /* number of used slots */
bool pw_store_set(int slot, const char *label, const char *pass);
bool pw_store_delete(int slot);
bool pw_store_get_label(int slot, char *out, int maxlen);
bool pw_store_get_pass(int slot, char *out, int maxlen);

#endif /* PW_STORE_H */
