#ifndef PB10A_TOOLS_MOTION_APP_H
#define PB10A_TOOLS_MOTION_APP_H
#include "ui_conf.h"
/* One foreground sensor tool at a time; share the existing PM block. */
#define SENSOR_TOOL_PM_BLOCK 0x20u
/* PB02B Leveler and 6-face accelerometer calibration, ported to MiaoUI.
 * Both are single-frame WORD apps. The sensor coroOS owns all I2C.
 * UI only reads an algorithm snapshot and records validated calibration. */
extern const unsigned char img_leveler[];
extern const unsigned char img_leveler_cal[];

void Motion_LevelerRun(ui_t *ui);
void Motion_LevelerOnLeave(ui_t *ui);
void Motion_CalibrationRun(ui_t *ui);
void Motion_CalibrationOnLeave(ui_t *ui);
#endif
