/* 5-key PB10A adapter; Collar-Radio Buttons drives the 10ms scan.
 * A short CONF release emits ENTER; CONF held 700ms emits BACK once.
 * Repeating directions are queued until MiaoUI can draw a new frame.
 */
#include "indev/indevDriver.h"
#include "display/dispDriver.h"
#include "buttons.h"
#include "pm_api.h"
#include "beep_service.h"
#include "time_service.h"
#define KEY_QUEUE_SIZE 16u
#define KEY_EVENT_REPEAT 0x80u
#define KEY_EVENT_ENTER ((uint8_t)KeyIndex_Max)
#define KEY_EVENT_BACK ((uint8_t)(KeyIndex_Max+1u))
static uint8_t queue[KEY_QUEUE_SIZE],head,tail;
static uint8_t conf_valid, conf_long, suppressed, action_repeat, s_continuous_lr, s_key_rotation;
static void clear_events(void){head=0u;tail=0u;}
void indevClearPending(void) {
    clear_events();
    conf_valid=0u;conf_long=0u;
    suppressed=0xFFu; /* held keys cannot act on the destination page */
}
static void queue_event(uint8_t ev) {
    uint8_t next=(uint8_t)((head+1u)%KEY_QUEUE_SIZE);
    if(next==tail)tail=(uint8_t)((tail+1u)%KEY_QUEUE_SIZE);
    queue[head]=ev;head=next;
}

uint8_t indevHasPending(void) {
    return (uint8_t)(head != tail);
}

void indevInit(void) {
    Key_Init();clear_events();
    conf_valid=0u;conf_long=0u;suppressed=0u;
    s_key_rotation = Disp_GetRotation();
}

/* Logical directions follow the frame already drawn on the OLED.
 * Game/app rotation locks use native R0; CONF never changes meaning. */
static uint8_t screen_key(uint8_t key, uint8_t flipped) {
    if (!flipped)
        return key;
    switch (key) {
    case KeyIndex_UP:
        return KeyIndex_DOWN;
    case KeyIndex_DOWN:
        return KeyIndex_UP;
    case KeyIndex_LEFT:
        return KeyIndex_RIGHT;
    case KeyIndex_RIGHT:
        return KeyIndex_LEFT;
    default:
        return key;
    }
}

uint8_t indevService_10ms(uint8_t continuous_lr) {
    uint8_t i, blocked, held = 0u;
    uint8_t mode_changed = s_continuous_lr != continuous_lr;
    s_continuous_lr = continuous_lr;
    uint8_t rotation = Disp_GetRotation();
    uint8_t rotation_changed = s_key_rotation != rotation;
    s_key_rotation = rotation;
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

        /* A held direction must not turn into a new opposite command when
         * the display rotates, especially during continuous WLED dimming. */
        if (rotation_changed && i != KeyIndex_CONF && edge != KeyEdge_Rising &&
            (state == KeyState_ShortPress || state == KeyState_LongPress))
            suppressed |= bit;

        if (i == KeyIndex_LEFT || i == KeyIndex_RIGHT) {
            if (mode_changed && edge != KeyEdge_Rising &&
                (state == KeyState_ShortPress || state == KeyState_LongPress))
                suppressed |= bit;
            if (continuous_lr) {
                if (!blocked && !(suppressed & bit) &&
                    (state == KeyState_ShortPress || state == KeyState_LongPress))
                    held |= (uint8_t)(1u << screen_key(i, rotation));
                continue;
            }
        }
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
            uint8_t click = KEY_GetDASClick((KeyIndex_t)i);
            if (click && !blocked && !(suppressed & bit)) {
                queue_event((uint8_t)(screen_key(i, rotation) | (edge == KeyEdge_Rising ? 0u : KEY_EVENT_REPEAT)));
                pm_api_refresh_idle();
            }
        }
    }
    if(blocked)conf_valid=0u;
    return held;
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

uint8_t indevActionIsRepeat(void) {
    return action_repeat;
}

UI_ACTION indevScan(const ui_t *ui) {
    UI_ACTION result;
    uint8_t ev;
    action_repeat = 0u;
    if(pm_api_ui_should_block() || pm_api_is_unstable_wake())
        {clear_events();return UI_ACTION_NONE;}
    if(head==tail)return UI_ACTION_NONE;
    ev=queue[tail];tail=(uint8_t)((tail+1u)%KEY_QUEUE_SIZE);
    action_repeat = (uint8_t)((ev & KEY_EVENT_REPEAT) != 0u);
    result = translate((uint8_t)(ev & ~KEY_EVENT_REPEAT), ui);
    /* The ringing WORD page owns all actions, including long CONF/BACK.
     * Its explicit exit after dismissal is not subject to this filter. */
    if (ui && ui->nowItem == Alarm_RingItem() &&
        (ui->menuState == UI_ITEM_DRAWING || ui->menuState == UI_ITEM_RUNING)) {
        Alarm_HandleAction(result, action_repeat);
        return UI_ACTION_NONE;
    }
    if(result!=UI_ACTION_NONE)beep_key_click();
    return result;
}
