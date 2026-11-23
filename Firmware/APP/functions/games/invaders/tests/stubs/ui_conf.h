#ifndef HOST_UI_CONF_H
#define HOST_UI_CONF_H
#include <stdint.h>
typedef enum { UI_ACTION_NONE=0, UI_ACTION_ENTER, UI_ACTION_BACK, UI_ACTION_UP, UI_ACTION_DOWN } UI_ACTION;
typedef struct { UI_ACTION action; } ui_t;
#endif
