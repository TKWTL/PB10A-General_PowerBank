/* PB10A five-key active-low adapter. Map physical directions by page type,
 * as in Collar-Radio's page-dependent key mapping table.
 *
 * ICON: LEFT/RIGHT = previous/next; UP/DOWN = minus/plus.
 * TEXT: UP/DOWN = previous/next; LEFT/RIGHT = minus/plus.
 * CONF short release = ENTER; hold >=700 ms = BACK (no sixth physical key).
 * The same mapping applies while a page's WORD/DATA widget is running.
 */
#include "indev/indevDriver.h"
#include "main.h"
#include "millis.h"
#include "pm_api.h"
#include "beep_algo.h"

#define KEY_DEBOUNCE_MS 25u
#define KEY_BACK_HOLD_MS 700u

enum {
    KEY_UP_BIT    = 1u << 0,
    KEY_DOWN_BIT  = 1u << 1,
    KEY_LEFT_BIT  = 1u << 2,
    KEY_RIGHT_BIT = 1u << 3,
    KEY_CONF_BIT  = 1u << 4
};

static uint8_t key_read_mask(void)
{
    uint8_t mask = 0u;
    if(!LL_GPIO_IsInputPinSet(UP_GPIO_Port, UP_Pin)) mask |= KEY_UP_BIT;
    if(!LL_GPIO_IsInputPinSet(DOWN_GPIO_Port, DOWN_Pin)) mask |= KEY_DOWN_BIT;
    if(!LL_GPIO_IsInputPinSet(LEFT_GPIO_Port, LEFT_Pin)) mask |= KEY_LEFT_BIT;
    if(!LL_GPIO_IsInputPinSet(RIGHT_GPIO_Port, RIGHT_Pin)) mask |= KEY_RIGHT_BIT;
    if(!LL_GPIO_IsInputPinSet(CONF_GPIO_Port, CONF_Pin)) mask |= KEY_CONF_BIT;
    return mask;
}

static UI_ACTION key_map_direction(uint8_t pressed, const ui_t *ui)
{
    const uint8_t text_page = (uint8_t)(ui != NULL &&
        ui->nowItem != NULL &&
        ui->nowItem->page.location != NULL &&
        ui->nowItem->page.location->type == UI_PAGE_TEXT);

    if(pressed & KEY_LEFT_BIT) return text_page ? UI_ACTION_MINUS : UI_ACTION_UP;
    if(pressed & KEY_RIGHT_BIT) return text_page ? UI_ACTION_PLUS : UI_ACTION_DOWN;
    if(pressed & KEY_UP_BIT) return text_page ? UI_ACTION_UP : UI_ACTION_MINUS;
    if(pressed & KEY_DOWN_BIT) return text_page ? UI_ACTION_DOWN : UI_ACTION_PLUS;
    return UI_ACTION_NONE;
}

UI_ACTION indevScan(const ui_t *ui)
{
    static uint8_t raw_last, stable, conf_armed, conf_back_sent;
    static uint32_t changed_at, conf_pressed_at;
    const uint8_t raw = key_read_mask();
    const uint32_t now = Millis();

    if(raw != raw_last)
    {
        raw_last = raw;
        changed_at = now;
    }

    if((uint32_t)(now - changed_at) >= KEY_DEBOUNCE_MS && raw != stable)
    {
        const uint8_t pressed = (uint8_t)(raw & (uint8_t)~stable);
        const uint8_t released = (uint8_t)(stable & (uint8_t)~raw);
        UI_ACTION action;
        stable = raw;

        if(pressed) pm_api_refresh_idle();

        if(pressed & KEY_CONF_BIT)
        {
            conf_pressed_at = now;
            conf_armed = (uint8_t)!pm_api_is_unstable_wake();
            conf_back_sent = 0u;
        }
        if(released & KEY_CONF_BIT)
        {
            const uint8_t enter = (uint8_t)(conf_armed &&
                !conf_back_sent && !pm_api_is_unstable_wake());
            conf_armed = 0u;
            conf_back_sent = 0u;
            if(enter)
            {
                beep_key_click();
                return UI_ACTION_ENTER;
            }
        }

        if(pm_api_is_unstable_wake()) return UI_ACTION_NONE;

        action = key_map_direction(pressed, ui);
        if(action != UI_ACTION_NONE)
        {
            beep_key_click();
            return action;
        }
    }

    /* A long CONF emits BACK once; release must not also emit ENTER.
     * Suppress presses that started during the unstable STOP1 wake period. */
    if((stable & KEY_CONF_BIT) && conf_armed && !conf_back_sent &&
       !pm_api_is_unstable_wake() &&
       (uint32_t)(now - conf_pressed_at) >= KEY_BACK_HOLD_MS)
    {
        conf_back_sent = 1u;
        pm_api_refresh_idle();
        beep_key_click();
        return UI_ACTION_BACK;
    }
    return UI_ACTION_NONE;
}
