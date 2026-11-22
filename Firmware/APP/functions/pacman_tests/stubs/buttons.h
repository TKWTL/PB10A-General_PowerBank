typedef enum {KeyIndex_UP,KeyIndex_DOWN,KeyIndex_LEFT,KeyIndex_RIGHT,KeyIndex_CONF,KeyIndex_Max} KeyIndex_t;
typedef enum {KeyState_None,KeyState_ShortPress,KeyState_LongPress,KeyState_Release} KeyState_t;
KeyState_t KEY_GetState(KeyIndex_t index);
