#ifndef PM_DEVICE_H
#define PM_DEVICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
    /* Hooks run in the cooperative foreground. PENDING retries next PM poll. */
    typedef enum {PM_HOOK_DONE=0,PM_HOOK_PENDING=1,PM_HOOK_ERROR=2} pm_hook_result_t;
    typedef pm_hook_result_t (*pm_hook_t)(void *ctx);
    typedef void (*pm_platform_hook_t)(void *ctx);
    typedef struct {
        const char *name;
        pm_hook_t on_prepare,on_suspend,on_resume;
        void *ctx;
        uint8_t order;
        pm_platform_hook_t on_deepsleep,on_standby;
    } pm_device_node_t;
    int pm_device_register(const pm_device_node_t *node);
    pm_hook_result_t pm_device_prepare_all(void);
    pm_hook_result_t pm_device_suspend_all(void);
    pm_hook_result_t pm_device_resume_all(void);
    void pm_device_abort_sequence(void);
    const char *pm_device_last_error(void);
    void pm_device_enter_deepsleep(void);
    void pm_device_enter_standby(void);
    void pm_device_register_builtin(void);
    void pm_device_register_linker_set(void);
    void pm_device_register_all(void);
#ifdef __cplusplus
}
#endif
#endif
