#ifndef PB10A_BATTERY_ALGO_H
#define PB10A_BATTERY_ALGO_H

#include <stdint.h>
#include "sw6306.h"

#define BAT_REFERENCE_UNKNOWN_MWH 9999000u /* 9999 Wh until first learning. */
#define BAT_EFC_SCALE 10000u

typedef struct {
    uint32_t full_mwh;
    uint32_t soh_d10; /* 0.1%, valid when soh_valid is set. */
    uint32_t efc_q10000; /* Settled cycles plus the valid active session estimate. */
    sw6306_learn_state_t learn;
    uint8_t soh_valid;
} battery_view_t;

void BatteryAlgo_Init(void);
/* Returns 1 only on the first valid observation of a learned capacity. */
uint8_t BatteryAlgo_Update(uint32_t full_mwh, uint32_t remain_mwh,
                           sw6306_learn_state_t learn, uint8_t direction_valid, uint8_t discharging);
void BatteryAlgo_InvalidSample(void);
void BatteryAlgo_Invalidate(void); /* Chip reinit: discard the unconfirmed session. */
void BatteryAlgo_Finalize(void); /* STOP1: settle through the last valid discharge sample. */
uint8_t BatteryAlgo_LearnRearmPending(void);
void BatteryAlgo_LearnRearmed(void);
void BatteryAlgo_GetView(battery_view_t *out);

#endif
