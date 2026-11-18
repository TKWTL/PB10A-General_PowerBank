#ifndef PB10A_APP_TASK_H
#define PB10A_APP_TASK_H
/* Call after the CubeMX-generated peripheral init sequence. */
void APP_TaskInit(void);
/* Run the single shared cooperative scheduler from the foreground loop. */
void APP_TaskRunOnce(void);
#endif
