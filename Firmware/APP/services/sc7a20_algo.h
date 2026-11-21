#ifndef PB10A_SC7A20_ALGO_H
#define PB10A_SC7A20_ALGO_H
#include "ui_conf.h"
void SC7A20Algo_Init(void);
void SC7A20Algo_Poll(void);
enum {SC7A20_ORIENT_UNKNOWN=0,SC7A20_ORIENT_NORMAL,SC7A20_ORIENT_FLIPPED};
/* Returns 0 for an unknown/sideways attitude; otherwise writes flip flag. */
uint8_t SC7A20Algo_GetRotation(uint8_t *flipped);
/* EXTI5 ISR may use this without I2C to gate a wake notification. */
uint8_t SC7A20Algo_WakeIRQ(void);
void SC7A20Algo_AddWakeSettingsItems(ui_page_t *page);
void SC7A20Algo_AddStatisticsItems(ui_page_t *page);
/* Sensor coroutine is the sole I2C writer; UI reads snapshots only.
 * mode: 0 normal 5Hz, 1 leveler 25Hz, 2 six-face calibration 5Hz. */
typedef struct {
    int16_t raw[3];      /* uncalibrated right-aligned 12-bit codes */
    int16_t mg[3];       /* calibrated milligravity */
    uint32_t seq;
} sc7a20_sample_t;
void SC7A20Algo_SetToolMode(uint8_t mode);
uint8_t SC7A20Algo_ToolMode(void);
uint8_t SC7A20Algo_GetSample(sc7a20_sample_t *out);
/* Face order X-,X+,Y-,Y+,Z-,Z+. Validated/calibrated atomically.
 * Compact calibration saved with CRC to RTC backup DR1..DR4. */
uint8_t SC7A20Algo_ApplySixFace(const int16_t face_raw[6][3]);

#endif
