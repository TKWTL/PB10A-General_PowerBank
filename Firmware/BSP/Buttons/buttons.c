/* PB10A adaptation of Collar-Radio Buttons. Poll at 10ms.
 * Four arrows: 30ms debounce, 450ms first DAS, 100ms repeat.
 * CONF: 30ms debounce, 700ms hold, no automatic repeat.
 */
#include "buttons.h"
#include "main.h"
#define DEBOUNCE_TICKS 3u
typedef uint8_t (*key_reader_t)(void);
typedef struct {uint8_t long_ticks,das_ticks,active_low;key_reader_t read;} key_cfg_t;
typedef struct {
    uint8_t sample,stable,ticks;
    KeyState_t state,prev;
    KeyEdge_t edge;
} key_state_t;
static key_state_t keys[KeyIndex_Max];
static uint8_t read_up(void){return (uint8_t)(LL_GPIO_IsInputPinSet(UP_GPIO_Port,UP_Pin)!=0u);}
static uint8_t read_down(void){return (uint8_t)(LL_GPIO_IsInputPinSet(DOWN_GPIO_Port,DOWN_Pin)!=0u);}
static uint8_t read_left(void){return (uint8_t)(LL_GPIO_IsInputPinSet(LEFT_GPIO_Port,LEFT_Pin)!=0u);}
static uint8_t read_right(void){return (uint8_t)(LL_GPIO_IsInputPinSet(RIGHT_GPIO_Port,RIGHT_Pin)!=0u);}
static uint8_t read_conf(void){return (uint8_t)(LL_GPIO_IsInputPinSet(CONF_GPIO_Port,CONF_Pin)!=0u);}
static const key_cfg_t cfg[KeyIndex_Max] = {
    {45u, 10u, 1u, read_up},
    {45u, 10u, 1u, read_down},
    {45u, 10u, 1u, read_left},
    {45u, 10u, 1u, read_right},
    {70u, 0u, 1u, read_conf}};
void Key_Init(void) {
    uint8_t i;
    for(i=0u;i<KeyIndex_Max;i++) {
        keys[i].sample=0u;keys[i].stable=0u;keys[i].ticks=0u;
        keys[i].state=KeyState_None;keys[i].prev=KeyState_None;
        keys[i].edge=KeyEdge_Null;
    }
}

void Key_DebounceService_10ms(void) {
    uint8_t i;for(i=0u;i<KeyIndex_Max;i++)if(keys[i].ticks)keys[i].ticks--;
}

void Key_Scand(void) {
    uint8_t i,raw;
    key_state_t *k;
    for(i=0u;i<KeyIndex_Max;i++) {
        k=&keys[i];raw=cfg[i].read();
        raw=cfg[i].active_low?(uint8_t)!raw:raw;
        if(raw!=k->sample){k->sample=raw;k->ticks=DEBOUNCE_TICKS;}
        if(k->stable!=k->sample && k->ticks==0u) {
            k->state=k->sample?KeyState_ShortPress:KeyState_Release;
            k->stable=k->sample;
            if(k->state==KeyState_ShortPress)k->ticks=cfg[i].long_ticks;
        }
        if(k->state==KeyState_ShortPress && k->ticks==0u)
            {k->state=KeyState_LongPress;k->ticks=cfg[i].das_ticks;}
        if(k->state==k->prev)k->edge=KeyEdge_Null;
        else if(k->state==KeyState_ShortPress &&
            (k->prev==KeyState_None || k->prev==KeyState_Release))
        k->edge=KeyEdge_Rising;
        else if(k->state==KeyState_Release &&
            (k->prev==KeyState_ShortPress || k->prev==KeyState_LongPress))
        k->edge=KeyEdge_Falling;
        else if(k->state==KeyState_LongPress && k->prev==KeyState_ShortPress)
            k->edge=KeyEdge_Holding;
        else k->edge=KeyEdge_Error;
        k->prev=k->state;
    }
}
KeyEdge_t Key_EdgeDetect(KeyIndex_t key)
{return key<KeyIndex_Max?keys[key].edge:KeyEdge_Error;}
KeyState_t KEY_GetState(KeyIndex_t key)
{return key<KeyIndex_Max?keys[key].state:KeyState_None;}
uint8_t KEY_GetDASClick(KeyIndex_t key) {
    key_state_t *k;
    if(key>=KeyIndex_CONF)return 0u;
    k=&keys[key];
    if(k->edge==KeyEdge_Rising || k->edge==KeyEdge_Holding)return 1u;
    if(cfg[key].das_ticks && k->state==KeyState_LongPress && k->ticks==0u)
        {k->ticks=cfg[key].das_ticks;return 1u;}
    return 0u;
}
