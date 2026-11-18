/* PB10A: one bus / one I2C1 HAL handle / interrupt-based transfers.
 * Calls are synchronous from caller perspective to preserve legacy PB02B
 * drivers. The full high-level operation owns the transaction lock, not each
 * START-to-STOP frame. No coroOS yield is legal while the lock is held.
 */
#include "bsp_i2c.h"
#include "i2c.h"
#include "millis.h"
#define I2C_TX_TIMEOUT_MS 30u
#define I2C_ABORT_TIMEOUT_MS 10u
enum {IT_IDLE=0u,IT_BUSY=1u,IT_DONE=2u,IT_FAILED=3u,IT_ABORTING=4u};
static volatile uint8_t s_it_state;
static uint8_t s_lock_depth; /* one foreground caller, nested driver calls */
static uint8_t s_poisoned;
static uint16_t s_active_address;
static uint8_t s_probe_dummy;
uint8_t I2C_TransactionBegin(void)
{
    /* Nested acquisition is valid only in the same non-yielding call stack.
     * All applications use a single cooperative foreground thread. */
    if(s_poisoned || s_lock_depth==255u)return I2C_ERROR;
    ++s_lock_depth;
    return I2C_OK;
}
void I2C_TransactionEnd(void)
{
    if(s_lock_depth)s_lock_depth--;
}
uint8_t I2C_BusIsLocked(void)
{return s_lock_depth!=0u || s_it_state==IT_BUSY || s_it_state==IT_ABORTING;}
static uint8_t i2c_wait(uint16_t address,HAL_StatusTypeDef started)
{
    uint32_t start=Millis();
    if(started!=HAL_OK)
    {s_it_state=IT_IDLE;return I2C_ERROR;}
    while(s_it_state==IT_BUSY)
    {
        if((uint32_t)(Millis()-start)<I2C_TX_TIMEOUT_MS)continue;
        s_it_state=IT_ABORTING;
        if(HAL_I2C_Master_Abort_IT(&hi2c1,address)!=HAL_OK)
        {
            s_poisoned=1u;s_it_state=IT_FAILED;
            return I2C_ERROR;
        }
        start=Millis();
        while(s_it_state==IT_ABORTING)
        {
            if((uint32_t)(Millis()-start)<I2C_ABORT_TIMEOUT_MS)continue;
            s_poisoned=1u;s_it_state=IT_FAILED;
            return I2C_ERROR;
        }
        s_it_state=IT_IDLE;
        return I2C_ERROR;
    }
    if(s_it_state==IT_DONE)
    {s_it_state=IT_IDLE;return I2C_OK;}
    s_it_state=IT_IDLE;
    return I2C_ERROR;
}
static uint8_t i2c_start(uint16_t address,uint8_t op,uint8_t reg,uint8_t *data,uint16_t count)
{
    HAL_StatusTypeDef result;
    if(s_poisoned || s_it_state!=IT_IDLE ||
       HAL_I2C_GetState(&hi2c1)!=HAL_I2C_STATE_READY)return I2C_ERROR;
    s_active_address=address;
    s_it_state=IT_BUSY;
    switch(op)
    {
    case 0u:result=HAL_I2C_Mem_Write_IT(&hi2c1,address,reg,I2C_MEMADD_SIZE_8BIT,data,count);break;
    case 1u:result=HAL_I2C_Mem_Read_IT(&hi2c1,address,reg,I2C_MEMADD_SIZE_8BIT,data,count);break;
    case 2u:result=HAL_I2C_Master_Transmit_IT(&hi2c1,address,data,count);break;
    case 3u:result=HAL_I2C_Master_Receive_IT(&hi2c1,address,data,count);break;
    default:s_it_state=IT_IDLE;return I2C_ERROR;
    }
    return i2c_wait(s_active_address,result);
}
uint8_t I2C_RegWrite(uint16_t a,uint8_t reg,const uint8_t *p,uint16_t n)
{
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_start(a,0u,reg,(uint8_t *)(void *)p,n);
    I2C_TransactionEnd();
    return result;
}
uint8_t I2C_RegRead(uint16_t a,uint8_t reg,uint8_t *p,uint16_t n)
{
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_start(a,1u,reg,p,n);
    I2C_TransactionEnd();
    return result;
}
uint8_t I2C_Transmit(uint16_t a,const uint8_t *p,uint16_t n)
{
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_start(a,2u,0u,(uint8_t *)(void *)p,n);
    I2C_TransactionEnd();
    return result;
}
uint8_t I2C_Receive(uint16_t a,uint8_t *p,uint16_t n)
{
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_start(a,3u,0u,p,n);
    I2C_TransactionEnd();
    return result;
}
uint8_t I2C_IsReady(uint16_t address)
{
    uint8_t result;
    if(I2C_TransactionBegin()!=I2C_OK)return 0u;
    /* Verified with bundled STM32G0 HAL source: Master_Transmit_IT with
     * Size=0 sends START/address/STOP (no TXDR byte) and reports via IRQ. */
    result=i2c_start(address,2u,0u,&s_probe_dummy,0u);
    I2C_TransactionEnd();
    return result==I2C_OK?1u:0u;
}
/* HAL_IRQ dispatch stays in CubeMX-generated stm32g0xx_it.c. */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_BUSY)s_it_state=IT_DONE;}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_BUSY)s_it_state=IT_DONE;}
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_BUSY)s_it_state=IT_DONE;}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_BUSY)s_it_state=IT_DONE;}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_BUSY)s_it_state=IT_FAILED;}
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h)
{if(h->Instance==I2C1 && s_it_state==IT_ABORTING)s_it_state=IT_IDLE;}
