/* PB10A SSD1306 128x64 4-wire SPI, single 1024-byte framebuffer DMA.
 * Preserve the original SSD1306 init sequence (page addressing).
 * Switch to horizontal addressing only for a frame transaction; restore
 * page addressing afterwards. Never write u8g2 RAM while DMA owns it.
 */
#include "ssd1306_display.h"
#include "spi.h"
#include "main.h"
#include "millis.h"
#include <string.h>
enum {OLED_PHASE_IDLE=0,OLED_PHASE_INIT,OLED_PHASE_ADDRESS,OLED_PHASE_FRAME,
      OLED_PHASE_RESTORE,OLED_PHASE_POWER,OLED_PHASE_CONTRAST};
#define OLED_TX_TIMEOUT_MS 50u
#define OLED_INIT_MAX_ATTEMPTS 3u
static u8g2_t *s_screen;
/* All DMA sources have static storage. No intermediate framebuffer copies. */
static const uint8_t *s_frame;
static uint8_t s_cmd[8];
static uint8_t s_init_cmd[]={
    0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,
    0x8D,0x14,0x20,0x02,0xA1,0xC8,0xDA,0x12,
    0x81,0xCF,0xD9,0xF1,0xDB,0x40,0x2E,0xA4,0xA6,0xAF
};
static uint8_t s_restore_cmd[]={0x20,0x02};
static uint8_t s_power_save,s_power_requested,s_power_dirty;
static uint8_t s_contrast_dirty,s_contrast,s_transmitted_value;
static uint8_t s_ready,s_init_pending,s_init_failed,s_init_attempts,s_dma_phase;
static uint32_t s_frames,s_dma_started_ms,s_power_ready_ms;
static SSD1306_DisplayStats s_stats;

static uint8_t OLED_U8X8Byte(u8x8_t *u8x8,uint8_t msg,uint8_t arg_int,void *arg_ptr)
{
    (void)u8x8;(void)arg_int;(void)arg_ptr;
    return (msg==U8X8_MSG_BYTE_SEND)?0u:1u;
}
static uint8_t OLED_U8X8Gpio(u8x8_t *u8x8,uint8_t msg,uint8_t arg_int,void *arg_ptr)
{
    (void)u8x8;(void)arg_int;(void)arg_ptr;
    return (msg==U8X8_MSG_DELAY_MILLI)?0u:1u;
}
static uint8_t oled_start(const uint8_t *data,uint16_t length,uint8_t data_mode,uint8_t phase)
{
    if(s_dma_phase!=OLED_PHASE_IDLE || SPI1_DmaTxState()!=SPI1_DMA_IDLE)
    {
        ++s_stats.dma_busy;
        return 0u;
    }
    if(data_mode) LL_GPIO_SetOutputPin(D_C_GPIO_Port,D_C_Pin);
    else LL_GPIO_ResetOutputPin(D_C_GPIO_Port,D_C_Pin);
    s_dma_started_ms=Millis();
    if(!SPI1_DmaTxStart(data,length))
    {
        ++s_stats.dma_start_failed;
        if(SPI1_DmaTxState()==SPI1_DMA_ERROR)
            (void)SPI1_DmaTxAbortAsync();
        return 0u;
    }
    s_dma_phase=phase;
    return 1u;
}
static void oled_failed(uint8_t phase)
{
    s_dma_phase=OLED_PHASE_IDLE;
    if(phase==OLED_PHASE_INIT)
    {
        ++s_stats.init_timeouts;
        if(++s_init_attempts>=OLED_INIT_MAX_ATTEMPTS)
        { s_init_pending=0u;s_init_failed=1u; }
    }
    /* For any failed frame phase, restart from address setup. Keep locked
     * framebuffer untouched until the full frame AND restore have succeeded. */
}
uint8_t SSD1306_Display_Begin(u8g2_t *screen)
{
    if(!screen)return 0u;
    LL_GPIO_SetOutputPin(CPEN_GPIO_Port,CPEN_Pin);
    s_power_ready_ms=Millis()+30u;
    s_screen=screen;s_frame=0;
    s_power_save=0u;s_power_requested=0u;s_power_dirty=0u;
    s_contrast_dirty=0u;s_contrast=0u;s_transmitted_value=0u;
    s_ready=0u;s_init_pending=1u;s_init_failed=0u;s_init_attempts=0u;
    s_dma_phase=OLED_PHASE_IDLE;s_frames=0u;
    memset(&s_stats,0,sizeof(s_stats));
    u8g2_Setup_ssd1306_128x64_f(screen,U8G2_R0,OLED_U8X8Byte,OLED_U8X8Gpio);
    u8g2_ClearBuffer(screen);
    return 1u;
}
SSD1306_InitState SSD1306_Display_InitState(void)
{
    if(s_ready)return SSD1306_INIT_READY;
    if(s_init_failed)return SSD1306_INIT_FAILED;
    return SSD1306_INIT_PENDING;
}
void SSD1306_Display_RequestFlush(const uint8_t *framebuffer)
{
    if(!s_ready || !framebuffer || s_frame)return;
    /* No memcpy: caller must not modify framebuffer until FrameBusy clears. */
    s_frame=framebuffer;
}
uint8_t SSD1306_Display_FrameBusy(void){return s_frame!=0;}
void SSD1306_Display_RequestContrast(uint8_t contrast)
{ s_contrast=contrast;s_contrast_dirty=1u; }
void SSD1306_Display_RequestPowerSave(uint8_t enable)
{ s_power_requested=enable?1u:0u;s_power_dirty=1u; }
void SSD1306_Display_Service(void)
{
    SPI1_DmaState status;
    uint8_t phase;
    if(!s_screen || s_init_failed)return;
    if(s_dma_phase!=OLED_PHASE_IDLE)
    {
        phase=s_dma_phase;
        status=SPI1_DmaTxState();
        if(status==SPI1_DMA_BUSY)
        {
            if((uint32_t)(Millis()-s_dma_started_ms)<=OLED_TX_TIMEOUT_MS)
                return;
            ++s_stats.dma_runtime_timeouts;
            if(!SPI1_DmaTxAbortAsync())oled_failed(phase);
            return;
        }
        if(status==SPI1_DMA_ABORTING)return;
        if(status==SPI1_DMA_ERROR)
        {
            ++s_stats.dma_irq_errors;
            if(!SPI1_DmaTxAbortAsync())oled_failed(phase);
            return;
        }
        if(status==SPI1_DMA_IDLE)
        {
            oled_failed(phase);
            return;
        }
        if(status!=SPI1_DMA_DONE)return;
        (void)SPI1_DmaTxRelease();
        s_dma_phase=OLED_PHASE_IDLE;
        switch(phase)
        {
        case OLED_PHASE_INIT:
            s_init_pending=0u;s_ready=1u;
            return;
        case OLED_PHASE_ADDRESS:
            (void)oled_start(s_frame,SSD1306_FB_SIZE,1u,OLED_PHASE_FRAME);
            return;
        case OLED_PHASE_FRAME:
            (void)oled_start(s_restore_cmd,sizeof(s_restore_cmd),0u,OLED_PHASE_RESTORE);
            return;
        case OLED_PHASE_RESTORE:
            s_frame=0;++s_frames;
            return;
        case OLED_PHASE_POWER:
            s_power_save=s_transmitted_value;
            s_power_dirty=(s_power_requested!=s_transmitted_value);
            return;
        case OLED_PHASE_CONTRAST:
            s_contrast_dirty=(s_contrast!=s_transmitted_value);
            return;
        default:return;
        }
    }
    if(s_init_pending)
    {
        if((int32_t)(Millis()-s_power_ready_ms)<0)return;
        status=SPI1_DmaTxState();
        if(status==SPI1_DMA_ABORTING || status==SPI1_DMA_BUSY)return;
        if(status==SPI1_DMA_ERROR)
        { (void)SPI1_DmaTxAbortAsync();return; }
        if(!oled_start(s_init_cmd,sizeof(s_init_cmd),0u,OLED_PHASE_INIT))
            if(++s_init_attempts>=OLED_INIT_MAX_ATTEMPTS)
            {s_init_pending=0u;s_init_failed=1u;}
        return;
    }
    if(!s_ready)return;
    if(s_frame)
    {
        /* 0x20 0x00 horizontal addressing, all 128 columns, pages 0..7.
         * Original 0x20 0x02 page-mode initialization remains unchanged. */
        s_cmd[0]=0x20u;s_cmd[1]=0x00u;
        s_cmd[2]=0x21u;s_cmd[3]=0x00u;s_cmd[4]=0x7Fu;
        s_cmd[5]=0x22u;s_cmd[6]=0x00u;s_cmd[7]=0x07u;
        (void)oled_start(s_cmd,8u,0u,OLED_PHASE_ADDRESS);
        return;
    }
    if(s_power_dirty)
    {
        s_transmitted_value=s_power_requested;
        s_cmd[0]=s_transmitted_value?0xAEu:0xAFu;
        (void)oled_start(s_cmd,1u,0u,OLED_PHASE_POWER);
        return;
    }
    if(s_contrast_dirty)
    {
        s_transmitted_value=s_contrast;
        s_cmd[0]=0x81u;s_cmd[1]=s_transmitted_value;
        (void)oled_start(s_cmd,2u,0u,OLED_PHASE_CONTRAST);
        return;
    }
}
uint8_t SSD1306_Display_IsReady(void){return s_ready;}
uint32_t SSD1306_Display_GetFrameCount(void){return s_frames;}
void SSD1306_Display_GetStats(SSD1306_DisplayStats *out)
{if(out)*out=s_stats;}
