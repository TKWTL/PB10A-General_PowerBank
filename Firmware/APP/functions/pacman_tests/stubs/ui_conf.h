#ifndef TEST_UI_CONF_H
#define TEST_UI_CONF_H
#include <stdint.h>
typedef enum {UI_ACTION_NONE,UI_ACTION_ENTER,UI_ACTION_BACK,UI_ACTION_UP,UI_ACTION_DOWN} UI_ACTION;
typedef struct ui_t { UI_ACTION action; } ui_t;
#define U8G2_DRAW_ALL 0xffu
#endif
