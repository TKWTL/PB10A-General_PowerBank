#ifndef HOST_BUTTONS_H
#define HOST_BUTTONS_H
typedef enum { KeyIndex_UP=0,KeyIndex_DOWN,KeyIndex_LEFT,KeyIndex_RIGHT,KeyIndex_CONF } KeyIndex_t;
typedef enum { KeyState_None=0,KeyState_ShortPress,KeyState_LongPress,KeyState_Release } KeyState_t;
KeyState_t KEY_GetState(KeyIndex_t key);
#endif
