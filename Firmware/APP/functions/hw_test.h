#ifndef PB10A_HW_TEST_H
#define PB10A_HW_TEST_H
#include <stdint.h>

/* TEMPORARY board bring-up, then change to 0:
 * 1: on boot automatically check auxiliary USB-A -> PB1 buzzer -> FM seek.
 * FM is ALWAYS MUTED at startup because VOL=0 is loud on this board.
 * Audio is manually enabled using ENTER inside Radio after tests.
 */
#define PB10A_HW_TEST_AUTORUN 1u
#define PB10A_HW_TEST_FM 1u
#define PB10A_HW_TEST_USB_MS 8000u
#define PB10A_HW_TEST_FM_MS 7000u

void HW_TestInit(void);
void HW_TestPoll(void);
uint8_t HW_TestActive(void);
#endif
