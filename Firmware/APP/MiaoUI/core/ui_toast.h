#ifndef MIAOUI_UI_TOAST_H
#define MIAOUI_UI_TOAST_H

#include <stdint.h>

/* Non-modal overlay; title/message are copied, and severity is 1..3.
 * BeginFrame/Draw/EndFrame are called only by the framebuffer owner. */
void UI_ToastShow(const char *title, const char *message, uint32_t duration_ms, uint8_t severity);
uint8_t UI_ToastNeedsFrame(void);
void UI_ToastBeginFrame(uint8_t allowed);
void UI_ToastSetAllowed(uint8_t allowed);
void UI_ToastDraw(void);
void UI_ToastSubmitted(uint8_t accepted);
void UI_ToastEndFrame(void);

#endif
