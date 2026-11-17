#ifndef PB10A_OLED_APP_H
#define PB10A_OLED_APP_H

/* Call after MX_GPIO_Init(), MX_DMA_Init(), MX_SPI1_Init(). */
void OLED_AppInit(void);
/* Run cooperative tasks as often as possible in main's while(1). */
void OLED_AppRunOnce(void);

#endif
