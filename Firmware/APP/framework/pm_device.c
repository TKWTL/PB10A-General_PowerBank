/* Ordered, resumable device PM. A completed node is not repeated while
 * the next hook reports PENDING. Resume walks nodes in reverse order. */
#include "pm_device.h"
#define PM_DEVICE_MAX 16u
enum {NONE=0,PREPARE,SUSPEND,RESUME};
static const pm_device_node_t *nodes[PM_DEVICE_MAX];
static uint8_t count,phase,cursor,resume_error;
static const char *last_error;
int pm_device_register(const pm_device_node_t *node) {
    uint8_t i,j;
    if(!node || count>=PM_DEVICE_MAX)return -1;
    for(j=0;j<count;j++)if(nodes[j]==node)return 0;
    i=count;
    while(i>0 && nodes[i-1]->order>node->order)
        {nodes[i]=nodes[i-1];--i;}
    nodes[i]=node;++count;return 0;
}

static pm_hook_result_t walk(uint8_t p) {
    pm_hook_result_t r;
    const pm_device_node_t *n;
    pm_hook_t h;
    if(phase!=p){phase=p;cursor=p==RESUME?count:0;resume_error=0;}
    while(p==RESUME?cursor>0:cursor<count) {
        n=nodes[p==RESUME?cursor-1:cursor];
        h=p==PREPARE?n->on_prepare:p==SUSPEND?n->on_suspend:n->on_resume;
        r=h?h(n->ctx):PM_HOOK_DONE;
        if(r==PM_HOOK_PENDING)return r;
        if(r==PM_HOOK_ERROR) {
            last_error=n->name;
            if(p!=RESUME){phase=NONE;return PM_HOOK_ERROR;}
            resume_error=1;/* best-effort restore other devices */
        }
        if(p==RESUME)--cursor;else ++cursor;
    }
    phase=NONE;
    return resume_error?PM_HOOK_ERROR:PM_HOOK_DONE;
}

pm_hook_result_t pm_device_prepare_all(void){return walk(PREPARE);}
pm_hook_result_t pm_device_suspend_all(void){return walk(SUSPEND);}
pm_hook_result_t pm_device_resume_all(void){return walk(RESUME);}
void pm_device_abort_sequence(void){phase=NONE;cursor=0;resume_error=0;}
const char *pm_device_last_error(void){return last_error;}
void pm_device_enter_deepsleep(void)
{uint8_t i;for(i=0;i<count;i++)if(nodes[i]->on_deepsleep)nodes[i]->on_deepsleep(nodes[i]->ctx);}
void pm_device_enter_standby(void)
{uint8_t i;for(i=0;i<count;i++)if(nodes[i]->on_standby)nodes[i]->on_standby(nodes[i]->ctx);}
__attribute__((weak)) void pm_device_register_linker_set(void){}
void pm_device_register_all(void){pm_device_register_builtin();pm_device_register_linker_set();}
