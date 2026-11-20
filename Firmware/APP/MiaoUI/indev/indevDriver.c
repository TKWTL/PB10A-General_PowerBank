/* 5-key PB10A adapter; Collar-Radio Buttons drives the 10ms scan.
* A short CONF release emits ENTER; CONF held 700ms emits BACK once.
* Repeating directions are queued until MiaoUI can draw a new frame.
*/
#include "indev/indevDriver.h"
#include "buttons.h"
#include "pm_api.h"
#include "beep_algo.h"
#define KEY_QUEUE_SIZE 16u
#define KEY_EVENT_ENTER ((uint8_t)KeyIndex_Max)
#define KEY_EVENT_BACK ((uint8_t)(KeyIndex_Max+1u))
static uint8_t queue[KEY_QUEUE_SIZE],head,tail;
static uint8_t conf_valid,conf_long,suppressed;
static void clear_events(void){head=0u;tail=0u;}
static void queue_event(uint8_t ev) {
    uint8_t next=(uint8_t)((head+1u)%KEY_QUEUE_SIZE);
    if(next==tail)tail=(uint8_t)((tail+1u)%KEY_QUEUE_SIZE);
    queue[head]=ev;head=next;
}
void indevInit(void) {
    Key_Init();clear_events();
    conf_valid=0u;conf_long=0u;suppressed=0u;
}
void indevService_10ms(void) {
    uint8_t i,blocked;
    KeyEdge_t edge;
    Key_DebounceService_10ms();Key_Scand();
    blocked=(uint8_t)(pm_api_ui_should_block() || pm_api_is_unstable_wake());
    if(blocked)clear_events();
    for(i=0u;i<KeyIndex_Max;i++) {
        uint8_t bit=(uint8_t)(1u<<i);
        KeyState_t state=KEY_GetState((KeyIndex_t)i);
        edge=Key_EdgeDetect((KeyIndex_t)i);
        if(state==KeyState_Release || state==KeyState_None)
        suppressed&=(uint8_t)~bit;
        else if(blocked)suppressed|=bit;
        if(i==KeyIndex_CONF) {
            if(edge==KeyEdge_Rising)
            {conf_valid=(uint8_t)(!blocked && !(suppressed&bit));conf_long=0u;}
            else if(edge==KeyEdge_Holding && conf_valid && !blocked) {
                clear_events();queue_event(KEY_EVENT_BACK);
                conf_long=1u;pm_api_refresh_idle();
            }
            else if(edge==KeyEdge_Falling) {
                if(conf_valid && !conf_long && !blocked) {
                    clear_events();queue_event(KEY_EVENT_ENTER);
                    pm_api_refresh_idle();
                }
                conf_valid=0u;conf_long=0u;
            }
        }
        else {
            uint8_t repeat=KEY_GetDASClick((KeyIndex_t)i);
            if(repeat && !blocked && !(suppressed&bit))
            {queue_event(i);pm_api_refresh_idle();}
        }
    }
    if(blocked)conf_valid=0u;
}
static UI_ACTION translate(uint8_t event,const ui_t *ui) {
    uint8_t is_text=(uint8_t)(ui && ui->nowItem && ui->nowItem->page.location &&
        ui->nowItem->page.location->type==UI_PAGE_TEXT);
    if(event==KEY_EVENT_ENTER)return UI_ACTION_ENTER;
    if(event==KEY_EVENT_BACK)return UI_ACTION_BACK;
    switch(event) {
    case KeyIndex_LEFT:return is_text?UI_ACTION_MINUS:UI_ACTION_UP;
    case KeyIndex_RIGHT:return is_text?UI_ACTION_PLUS:UI_ACTION_DOWN;
    case KeyIndex_UP:return is_text?UI_ACTION_UP:UI_ACTION_MINUS;
    case KeyIndex_DOWN:return is_text?UI_ACTION_DOWN:UI_ACTION_PLUS;
    default:return UI_ACTION_NONE;
    }
}
UI_ACTION indevScan(const ui_t *ui) {
    UI_ACTION result;
    uint8_t ev;
    if(pm_api_ui_should_block() || pm_api_is_unstable_wake())
    {clear_events();return UI_ACTION_NONE;}
    if(head==tail)return UI_ACTION_NONE;
    ev=queue[tail];tail=(uint8_t)((tail+1u)%KEY_QUEUE_SIZE);
    result=translate(ev,ui);
    if(result!=UI_ACTION_NONE)beep_key_click();
    return result;
}
