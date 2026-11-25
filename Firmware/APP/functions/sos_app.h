#ifndef PB10A_SOS_APP_H
#define PB10A_SOS_APP_H

#include "ui_conf.h"

/* Original PB10A SOS tool icon, 30 x 30 XBM; owned by this tool. */
extern const unsigned char img_sos[];

void SOS_AppRun(ui_t *ui);
void SOS_AppOnLeave(ui_t *ui);

#endif
