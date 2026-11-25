#ifndef PB10A_COMPASS_APP_H
#define PB10A_COMPASS_APP_H

#include "ui_conf.h"

/* Map native MMC5603 axes to SC7A20 native XYZ. Signed, one-based axes:
 * +1 = X, -2 = inverted Y, etc. PB10A inferred mounting: Y, -X, Z;
 * verify with the guided calibration and physical heading test. */
#ifndef COMPASS_MAG_X_AXIS
#define COMPASS_MAG_X_AXIS 2
#endif
#ifndef COMPASS_MAG_Y_AXIS
#define COMPASS_MAG_Y_AXIS -1
#endif
#ifndef COMPASS_MAG_Z_AXIS
#define COMPASS_MAG_Z_AXIS 3
#endif

extern const unsigned char img_compass[];
void Compass_AppRun(ui_t *ui);
void Compass_AppOnLeave(ui_t *ui);

#endif
