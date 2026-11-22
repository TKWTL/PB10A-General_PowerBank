/* MiaoUI WORD-page adapter for the PB10A Pac-Man-style game.
 * Reuses the native u8g2 framebuffer; SPI DMA owns that framebuffer
 * until the next MiaoUI pass. Never draws from another coroutine.
 */
#include "game_pacman.h"
#include "game_pacman_core.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "buttons.h"
#include "millis.h"
#include <stdio.h>

#define PACMAN_CELL_PX 5u
#define PACMAN_ORIGIN_X 2u
#define PACMAN_ORIGIN_Y 4u

static uint8_t s_active;

static pacman_dir_t pacman_read_dpad(void) {
    /* Read debounced, held key state. The menu's 1 s/200 ms DAS repeat
     * is intentionally NOT used for in-game movement. */
    static const KeyIndex_t buttons[4] = {
        KeyIndex_UP, KeyIndex_RIGHT, KeyIndex_DOWN, KeyIndex_LEFT
    };
    static const pacman_dir_t dirs[4] = {
        PACMAN_DIR_UP, PACMAN_DIR_RIGHT, PACMAN_DIR_DOWN, PACMAN_DIR_LEFT
    };
    for (uint8_t i = 0u; i < 4u; ++i) {
        KeyState_t state = KEY_GetState(buttons[i]);
        if (state == KeyState_ShortPress || state == KeyState_LongPress)
            return dirs[i];
    }
    return PACMAN_DIR_NONE;
}

static void draw_actor(uint8_t x, uint8_t y, uint8_t ghost,
                       uint8_t frightened, uint8_t direction,
                       uint32_t now_ms) {
    uint8_t white = 1u;
    uint8_t black = 0u;
    uint16_t cx = PACMAN_ORIGIN_X + (uint16_t)x * PACMAN_CELL_PX + 2u;
    uint16_t cy = PACMAN_ORIGIN_Y + (uint16_t)y * PACMAN_CELL_PX + 2u;
    Disp_SetDrawColor(&white);
    if (ghost) {
        if (frightened) Disp_DrawFrame(cx - 2u, cy - 2u, 5u, 5u);
        else Disp_DrawBox(cx - 2u, cy - 2u, 5u, 4u);
        if (!frightened) {
            Disp_SetDrawColor(&black);
            Disp_DrawPixel(cx - 1u, cy - 1u);
            Disp_DrawPixel(cx + 1u, cy - 1u);
        }
    } else {
        Disp_DrawDisc(cx, cy, 2u, U8G2_DRAW_ALL);
        if (((now_ms / 150u) & 1u) == 0u) {
            Disp_SetDrawColor(&black);
            if (direction == PACMAN_DIR_LEFT) Disp_DrawPixel(cx - 2u, cy);
            else if (direction == PACMAN_DIR_UP) Disp_DrawPixel(cx, cy - 2u);
            else if (direction == PACMAN_DIR_DOWN) Disp_DrawPixel(cx, cy + 2u);
            else Disp_DrawPixel(cx + 2u, cy);
        }
    }
}

static void draw_hud(const pacman_game_t *g) {
    char txt[10];
    uint8_t white = 1u;
    Disp_SetDrawColor(&white);
    Disp_SetFont(font_home_h6w4);
    Disp_DrawStr(110u, 10u, "SC");
    (void)snprintf(txt, sizeof(txt), "%04lu", (unsigned long)(g->score % 10000u));
    Disp_DrawStr(110u, 18u, txt);
    Disp_DrawStr(110u, 32u, "LV");
    (void)snprintf(txt, sizeof(txt), "%02u", (unsigned)g->level);
    Disp_DrawStr(110u, 40u, txt);
    Disp_DrawStr(110u, 53u, "HP");
    (void)snprintf(txt, sizeof(txt), "%u", (unsigned)g->lives);
    Disp_DrawStr(110u, 61u, txt);
}

static void draw_message(const char *line1, const char *line2) {
    uint8_t white = 1u, black = 0u;
    Disp_SetDrawColor(&black);
    Disp_DrawBox(19u, 22u, 88u, 24u);
    Disp_SetDrawColor(&white);
    Disp_DrawFrame(19u, 22u, 88u, 24u);
    Disp_SetFont(font_radio_h12w6);
    Disp_DrawStr(24u, 34u, line1);
    Disp_SetFont(font_home_h6w4);
    Disp_DrawStr(25u, 42u, line2);
}

static void draw_game(const pacman_game_t *g, uint32_t now_ms) {
    uint8_t white = 1u;
    uint8_t frightened = PacmanGame_PowerActive(now_ms);
    Disp_SetMaxClipWindow();
    Disp_ClearBuffer();
    Disp_SetDrawColor(&white);
    for (uint8_t y = 0u; y < PACMAN_GRID_HEIGHT; ++y) {
        for (uint8_t x = 0u; x < PACMAN_GRID_WIDTH; ++x) {
            uint8_t cell = g->cells[y][x];
            uint16_t px = PACMAN_ORIGIN_X + (uint16_t)x * PACMAN_CELL_PX;
            uint16_t py = PACMAN_ORIGIN_Y + (uint16_t)y * PACMAN_CELL_PX;
            if (cell == '#') Disp_DrawBox(px, py, 5u, 5u);
            else if (cell == '.') Disp_DrawPixel(px + 2u, py + 2u);
            else if (cell == 'o') Disp_DrawDisc(px + 2u, py + 2u, 1u, U8G2_DRAW_ALL);
        }
    }
    draw_actor(g->player_x, g->player_y, 0u, 0u, g->facing, now_ms);
    for (uint8_t i = 0u; i < PACMAN_GHOST_COUNT; ++i)
        draw_actor(g->ghosts[i].x, g->ghosts[i].y, 1u, frightened, 0u, now_ms);
    draw_hud(g);
    switch (g->phase) {
    case PACMAN_READY: draw_message("READY", "CONF: START"); break;
    case PACMAN_PAUSED: draw_message("PAUSED", "CONF: RESUME"); break;
    case PACMAN_DYING: draw_message("OUCH!", ""); break;
    case PACMAN_WON: draw_message("CLEARED", "CONF: NEXT"); break;
    case PACMAN_GAME_OVER: draw_message("GAME OVER", "CONF: RETRY"); break;
    default: break;
    }
    Disp_SendBuffer();
}

void Game_PacmanOnLeave(ui_t *ui) {
    (void)ui;
    if (!s_active) return;
    s_active = 0u;
    PacmanGame_EndSession();
}

void Game_PacmanRun(ui_t *ui) {
    uint32_t now;
    uint8_t confirm;
    static const uint8_t frame_delay[3] = {33u, 33u, 34u};
    static uint8_t frame_phase;
    if (!ui) return;
    if (ui->action == UI_ACTION_BACK) {
        Game_PacmanOnLeave(ui);
        return; /* MiaoUI handles the BACK page transition. */
    }
    now = Millis();
    if (!s_active) {
        PacmanGame_StartSession(now);
        s_active = 1u;
    }
    confirm = (uint8_t)(ui->action == UI_ACTION_ENTER);
    ui->action = UI_ACTION_NONE; /* Directional events must not exit WORD page. */
    PacmanGame_Update(now, pacman_read_dpad(), confirm);
    draw_game(PacmanGame_Get(), now);
    UI_RequestNextFrame(ui, frame_delay[frame_phase]);
    frame_phase = (uint8_t)((frame_phase + 1u) % 3u);
}
