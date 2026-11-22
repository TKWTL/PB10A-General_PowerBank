#include "ui_conf.h"
void Disp_SetMaxClipWindow(void);
void Disp_ClearBuffer(void);
void Disp_SetDrawColor(void *p);
void Disp_DrawBox(uint16_t x,uint16_t y,uint16_t w,uint16_t h);
void Disp_DrawFrame(uint16_t x,uint16_t y,uint16_t w,uint16_t h);
void Disp_DrawDisc(uint16_t x,uint16_t y,uint16_t r,uint8_t op);
void Disp_DrawPixel(uint16_t x,uint16_t y);
void Disp_SetFont(const uint8_t *font);
uint16_t Disp_DrawStr(uint16_t x,uint16_t y,const char *text);
void Disp_SendBuffer(void);
