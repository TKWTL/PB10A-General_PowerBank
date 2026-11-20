#ifndef IH_SSD1306_DISPLAY_H
#define IH_SSD1306_DISPLAY_H
#include <stdint.h>
#include "u8g2.h"
#define SSD1306_WIDTH 128u
#define SSD1306_HEIGHT 64u
#define SSD1306_FB_SIZE (SSD1306_WIDTH*SSD1306_HEIGHT/8u)
typedef enum {SSD1306_INIT_PENDING=0,SSD1306_INIT_READY,
    SSD1306_INIT_FAILED} SSD1306_InitState;
/* Begin always returns immediately; display becomes ready asynchronously
* after SSD1306_Display_Service() completes the SPI DMA init sequence. */
uint8_t SSD1306_Display_Begin(u8g2_t *screen);
SSD1306_InitState SSD1306_Display_InitState(void);
void SSD1306_Display_RequestFlush(const uint8_t *framebuffer);
void SSD1306_Display_RequestContrast(uint8_t contrast);
void SSD1306_Display_RequestPowerSave(uint8_t enable);
uint8_t SSD1306_Display_PowerSettled(uint8_t off);
void SSD1306_Display_Service(void);
/* Nonzero while u8g2 framebuffer is owned by the asynchronous SPI DMA. */
uint8_t SSD1306_Display_FrameBusy(void);
typedef struct {
    uint32_t init_timeouts;
    uint32_t dma_start_failed;
    uint32_t dma_busy;
    uint32_t dma_irq_errors;
    uint32_t dma_runtime_timeouts;
} SSD1306_DisplayStats;
void SSD1306_Display_GetStats(SSD1306_DisplayStats *out);
uint8_t SSD1306_Display_IsReady(void);
uint32_t SSD1306_Display_GetFrameCount(void);
#endif
