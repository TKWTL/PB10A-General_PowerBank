/* PB10A non-modal overlay for the single DMA-owned SSD1306 framebuffer. */
#include "core/ui_toast.h"
#include "display/dispDriver.h"
#include "ssd1306_display.h"
#include "millis.h"
#include "mini_format.h"
#include "i18n.h"
#include <string.h>

#define TOAST_WIDTH 114u
#define TOAST_HEIGHT 32u
#define TOAST_PORTRAIT_HEIGHT 48u

static char s_code[4], s_message[24];
static uint32_t s_duration, s_started;
static uint8_t s_severity, s_started_valid, s_allowed, s_dirty, s_on_screen;
/* Save only the affected physical page bytes, not a second framebuffer.
 * R0/R2: 114x4; R1/R3: 48x8. Both rectangles are centered. */
static uint8_t s_background[TOAST_WIDTH * (TOAST_HEIGHT / 8u)];
static uint8_t s_saved, s_x, s_page, s_width, s_pages;

static uint8_t toast_expired(void) {
    return s_started_valid && (uint32_t)(Millis() - s_started) >= s_duration;
}

void UI_ToastShow(const char *code, const char *message, uint32_t duration_ms, uint8_t severity) {
    if (s_severity && !toast_expired() && severity < s_severity)
        return;
    size_t len = strlen(code);
    if (len >= sizeof(s_code))
        len = sizeof(s_code) - 1u;
    memcpy(s_code, code, len);
    s_code[len] = '\0';
    len = strlen(message);
    if (len >= sizeof(s_message))
        len = sizeof(s_message) - 1u;
    memcpy(s_message, message, len);
    s_message[len] = '\0';
    s_duration = duration_ms;
    s_severity = severity;
    s_dirty = 1u;
    s_started_valid = 0u;
}

uint8_t UI_ToastNeedsFrame(void) {
    return s_dirty || toast_expired();
}

static void toast_restore(void) {
    if (!s_saved)
        return;
    uint8_t *buffer = Disp_GetBufferPtr();
    for (unsigned i = 0; i < s_pages; ++i)
        memcpy(buffer + (s_page + i) * 128u + s_x, s_background + i * s_width, s_width);
    s_saved = 0u;
}

void UI_ToastBeginFrame(uint8_t allowed) {
    /* The UI owner has already waited for DMA to release the buffer. */
    toast_restore();
    s_allowed = allowed;
    if (toast_expired()) {
        s_severity = s_started_valid = 0u;
        s_dirty = 1u;
    }
}

void UI_ToastSetAllowed(uint8_t allowed) {
    s_allowed = allowed;
}

static void toast_center(u8g2_t *draw, unsigned x, unsigned y, unsigned width, const char *text) {
    unsigned pixels = u8g2_GetUTF8Width(draw, text);
    u8g2_DrawUTF8(draw, x + (width > pixels ? (width - pixels) / 2u : 0u), y, text);
}

void UI_ToastDraw(void) {
    if (SSD1306_Display_FrameBusy() || s_saved)
        return;
    if (!s_allowed || !s_severity)
        return;

    /* Copy only drawing context; both contexts refer to the same buffer.
     * Font, XOR mode, rotation and clip state of the application survive. */
    u8g2_t draw = u8g2;
    uint8_t portrait = draw.width < draw.height;
    unsigned x = portrait ? 1u : 7u;
    unsigned y = portrait ? 40u : 16u;
    unsigned w = portrait ? 62u : TOAST_WIDTH;
    unsigned h = portrait ? TOAST_PORTRAIT_HEIGHT : TOAST_HEIGHT;
    s_x = portrait ? 40u : 7u;
    s_page = portrait ? 0u : 2u;
    s_width = portrait ? TOAST_PORTRAIT_HEIGHT : TOAST_WIDTH;
    s_pages = portrait ? 8u : TOAST_HEIGHT / 8u;
    uint8_t *buffer = Disp_GetBufferPtr();
    for (unsigned i = 0; i < s_pages; ++i)
        memcpy(s_background + i * s_width, buffer + (s_page + i) * 128u + s_x, s_width);
    s_saved = 1u;

    u8g2_SetMaxClipWindow(&draw);
    u8g2_SetClipWindow(&draw, x, y, x + w, y + h);
    u8g2_SetDrawColor(&draw, 0u);
    u8g2_DrawBox(&draw, x, y, w, h);
    u8g2_SetDrawColor(&draw, 1u);
    u8g2_DrawRFrame(&draw, x, y, w, h, 2u);
    if (s_severity == 3u)
        u8g2_DrawRFrame(&draw, x + 1u, y + 1u, w - 2u, h - 2u, 1u);
    if (s_severity >= 2u)
        u8g2_DrawBox(&draw, x + 2u, y + 2u, w - 4u, 13u);
    u8g2_SetFont(&draw, portrait ? font_home_h6w4 : UI_FONT);
    u8g2_SetFontMode(&draw, 1u);
    u8g2_SetFontDirection(&draw, 0u);
    u8g2_SetFontPosBaseline(&draw);
    u8g2_SetDrawColor(&draw, s_severity >= 2u ? 0u : 1u);
    /* Keep ASCII source keys in RAM so rotation never strands a
     * translated string in the h6w4 font. */
    char title[18];
    const char *level = s_severity == 3u ? "FAULT" : s_severity == 2u ? "WARNING"
                                                                      : "INFO";
    mini_snprintf(title, sizeof(title), "%s %s", s_code, portrait ? level : I18N_Text(level));
    toast_center(&draw, x, y + (portrait ? 11u : 13u), w, title);
    u8g2_SetDrawColor(&draw, 1u);
    if (!portrait) {
        toast_center(&draw, x, y + 27u, w, I18N_Text(s_message));
    } else {
        /* Portrait: use the existing 4px font and wrap at a space. */
        char line[24];
        memcpy(line, s_message, sizeof(line));
        if (u8g2_GetUTF8Width(&draw, line) > w - 4u) {
            size_t split = 14u;
            while (split && line[split] != ' ')
                --split;
            if (!split)
                split = 14u;
            char *next = line + split;
            if (*next == ' ') {
                *next++ = '\0';
            } else {
                /* All current fault strings fit or have a space. */
                line[split] = '\0';
                next = (char *)s_message + split;
            }
            toast_center(&draw, x, y + 25u, w, line);
            toast_center(&draw, x, y + 34u, w, next);
        } else {
            toast_center(&draw, x, y + 29u, w, line);
        }
    }
}

void UI_ToastSubmitted(uint8_t accepted) {
    if (!accepted) {
        if (s_saved) {
            toast_restore();
            s_dirty = 1u;
        }
        return;
    }
    if (!s_saved) {
        s_on_screen = 0u;
        if (!s_severity)
            s_dirty = 0u;
        return;
    }
    if (!s_started_valid) {
        s_started = Millis();
        s_started_valid = 1u;
    }
    s_dirty = 0u;
    s_on_screen = 1u;
}

void UI_ToastEndFrame(void) {
    /* Static/cached pages need a flush on overlay appearance or expiry.
     * For a normal flush, Disp_SendBuffer has already drawn the overlay. */
    if (!SSD1306_Display_FrameBusy() && s_allowed &&
        (s_dirty || (s_severity && !s_on_screen)))
        Disp_SendBuffer();
}
