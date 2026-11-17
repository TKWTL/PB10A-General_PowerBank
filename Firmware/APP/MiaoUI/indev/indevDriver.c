/* PB10A five-key active-low adapter using CubeMX UserLabel pins.
 * UP/DOWN navigate; LEFT backs out; RIGHT/CONF select.
 * Debounce by polling; EXTI is intentionally unused in this bring-up.
 */
#include "indev/indevDriver.h"
#include "main.h"
#include "pm_api.h"

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

UI_ACTION indevScan(void)
{
    static uint8_t raw_last, stable;
    static uint32_t changed_at;
    const uint8_t raw = key_read_mask();
    const uint32_t now = HAL_GetTick();
    uint8_t pressed;

    if(raw != raw_last)
    {
        raw_last = raw;
        changed_at = now;
        return UI_ACTION_NONE;
    }
    if((uint32_t)(now - changed_at) < 25u || raw == stable)
        return UI_ACTION_NONE;

    pressed = (uint8_t)(raw & (uint8_t)~stable);
    stable = raw;
    if(pressed)pm_api_refresh_idle();
    if(pm_api_is_unstable_wake())return UI_ACTION_NONE;
    if(pressed & KEY_CONF_BIT) return UI_ACTION_ENTER;
    if(pressed & KEY_LEFT_BIT) return UI_ACTION_BACK;
    if(pressed & KEY_RIGHT_BIT) return UI_ACTION_ENTER;
    if(pressed & KEY_UP_BIT) return UI_ACTION_UP;
    if(pressed & KEY_DOWN_BIT) return UI_ACTION_DOWN;
    return UI_ACTION_NONE;
}
