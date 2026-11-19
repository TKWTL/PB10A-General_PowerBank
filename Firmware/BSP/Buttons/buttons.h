/* PB10A port of Collar-Radio Drivers/Buttons/buttons.h:
 * 10ms sampling, debounced key edges and delayed auto shift (DAS).
 * Technical origin: https://blog.csdn.net/hxj_csdn/article/details/104318108
 */
#ifndef PB10A_BUTTONS_H
#define PB10A_BUTTONS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {KeyState_None=0,KeyState_ShortPress,KeyState_LongPress,KeyState_Release} KeyState_t;
typedef enum {KeyEdge_Null=0,KeyEdge_Rising,KeyEdge_Falling,KeyEdge_Holding,KeyEdge_Error} KeyEdge_t;
typedef enum {KeyIndex_UP=0,KeyIndex_DOWN,KeyIndex_LEFT,KeyIndex_RIGHT,KeyIndex_CONF,KeyIndex_Max} KeyIndex_t;
void Key_Init(void);
void Key_DebounceService_10ms(void);
void Key_Scand(void);
KeyEdge_t Key_EdgeDetect(KeyIndex_t key);
KeyState_t KEY_GetState(KeyIndex_t key);
uint8_t KEY_GetDASClick(KeyIndex_t key);
#ifdef __cplusplus
}
#endif
#endif
