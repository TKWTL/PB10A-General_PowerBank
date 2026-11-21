#ifndef PB10A_RADIO_ALGO_H
#define PB10A_RADIO_ALGO_H
#include <stdint.h>

/* Radio service is the sole owner of the RDA5807 async transport, FM
 * state machine and sorted station list. UI reads snapshots and posts
 * commands only; no direct I2C/MCU-clock operations in Radio_AppRun(). */
#define RADIO_MAX_STATIONS 50u
typedef enum {
    RADIO_STATE_OFF=0, RADIO_STATE_RESET_WAIT, RADIO_STATE_POWER_WAIT,
    RADIO_STATE_TUNE_WAIT, RADIO_STATE_SEARCH, RADIO_STATE_READY,
    RADIO_STATE_ERROR
} radio_state_t;
typedef enum {
    RADIO_MODE_SEARCH=0, RADIO_MODE_PRESET, RADIO_MODE_FREQ
} radio_mode_t;
typedef enum {
    RADIO_POWER_AUTO=0, RADIO_POWER_ON, RADIO_POWER_OFF
} radio_power_t;
typedef enum {
    RADIO_CMD_VIEW_ENTER=0, RADIO_CMD_VIEW_EXIT,
    RADIO_CMD_VOLUME_UP, RADIO_CMD_VOLUME_DOWN,
    RADIO_CMD_FREQUENCY_UP, RADIO_CMD_FREQUENCY_DOWN,
    RADIO_CMD_MODE_CYCLE, RADIO_CMD_POWER_CYCLE,
    RADIO_CMD_TOGGLE_STATION, RADIO_CMD_SCAN_ALL
} radio_command_t;
typedef struct {
    uint8_t state,mode,power_mode,volume,rssi,stereo;
    uint8_t searching,search_failed,station_count,error_code;
    uint16_t frequency;        /* 25 kHz units */
    uint16_t stations[RADIO_MAX_STATIONS];
} radio_snapshot_t;

void RadioAlgo_Init(void);
void RadioAlgo_Poll(void);     /* dedicated 5ms coroOS RadioServiceThread */
uint8_t RadioAlgo_Request(radio_command_t command);
void RadioAlgo_GetSnapshot(radio_snapshot_t *out);
uint8_t RadioAlgo_StopPending(void);
void RadioAlgo_Stop(void);     /* cooperative PM hook, enqueue only */
/* ON-mode receiver may keep playing across STOP1. WUT checks use a fresh
 * SW6306 ADC + capacity snapshot from its existing owner coroutine. */
uint8_t RadioAlgo_BackgroundWakeNeeded(void);
void RadioAlgo_RequestBatteryInspection(void);
uint8_t RadioAlgo_BootSleepAcknowledged(void);
#endif
