#include "../game_pacman_core.h"
#include <assert.h>
#include <stdio.h>

static void test_lifecycle(void) {
    const pacman_game_t *g;
    PacmanGame_StartSession(123u);
    g = PacmanGame_Get();
    assert(g->phase == PACMAN_READY && g->score == 0u && g->lives == 3u);
    assert(g->pellets_left > 0u);
    assert(g->cells[5][10] == ' ');
    assert(g->cells[0][0] == '#');
    PacmanGame_Update(200u, PACMAN_DIR_NONE, 1u);
    assert(g->phase == PACMAN_PLAYING);
    PacmanGame_Update(400u, PACMAN_DIR_LEFT, 0u);
    assert(g->player_x == 9u && g->score == 10u);
    PacmanGame_Update(450u, PACMAN_DIR_NONE, 1u);
    assert(g->phase == PACMAN_PAUSED);
    PacmanGame_Update(5000u, PACMAN_DIR_NONE, 0u);
    assert(g->phase == PACMAN_PAUSED);
    PacmanGame_Update(5001u, PACMAN_DIR_NONE, 1u);
    assert(g->phase == PACMAN_PLAYING);
    PacmanGame_EndSession();
    assert(g->score == 0u && g->pellets_left == 0u);
    PacmanGame_StartSession(5005u);
    assert(g->score == 0u && g->lives == 3u);
}

static void test_maze_reachability(void) {
    static unsigned char visited[PACMAN_GRID_HEIGHT][PACMAN_GRID_WIDTH];
    uint8_t xs[256], ys[256];
    unsigned head = 0u, tail = 0u, total = 0u;
    const pacman_game_t *g = PacmanGame_Get();
    for (unsigned y = 0u; y < PACMAN_GRID_HEIGHT; ++y)
        for (unsigned x = 0u; x < PACMAN_GRID_WIDTH; ++x)
            visited[y][x] = 0u;
    xs[tail] = g->player_x; ys[tail++] = g->player_y;
    visited[g->player_y][g->player_x] = 1u;
    while (head != tail) {
        unsigned x = xs[head], y = ys[head++];
        for (unsigned n = 0u; n < 4u; ++n) {
            int xx = (int)x + (n == 0 ? 1 : n == 1 ? -1 : 0);
            int yy = (int)y + (n == 2 ? 1 : n == 3 ? -1 : 0);
            if (y == 5u && xx == -1) xx = PACMAN_GRID_WIDTH - 1;
            if (y == 5u && xx == PACMAN_GRID_WIDTH) xx = 0;
            if (xx < 0 || xx >= (int)PACMAN_GRID_WIDTH || yy < 0 || yy >= (int)PACMAN_GRID_HEIGHT ||
                g->cells[yy][xx] == '#' || visited[yy][xx]) continue;
            visited[yy][xx] = 1u;
            xs[tail] = (uint8_t)xx; ys[tail++] = (uint8_t)yy;
        }
    }
    for (unsigned y = 0u; y < PACMAN_GRID_HEIGHT; ++y)
        for (unsigned x = 0u; x < PACMAN_GRID_WIDTH; ++x)
            if (g->cells[y][x] != '#') {
                assert(visited[y][x] != 0u);
                ++total;
            }
    assert(total > 90u);
}

static void test_long_random_session(void) {
    uint32_t now = 0u, seed = 0xFEE1DEADu;
    PacmanGame_StartSession(now);
    for (unsigned iter = 0u; iter < 15000u; ++iter) {
        const pacman_game_t *g = PacmanGame_Get();
        pacman_dir_t dir;
        uint8_t confirm = 0u;
        now += 33u;
        seed = seed * 1664525u + 1013904223u;
        dir = (pacman_dir_t)(seed % 5u);
        if (g->phase == PACMAN_READY || g->phase == PACMAN_PAUSED ||
            g->phase == PACMAN_WON || g->phase == PACMAN_GAME_OVER)
            confirm = 1u;
        PacmanGame_Update(now, dir, confirm);
        g = PacmanGame_Get();
        assert(g->player_x < PACMAN_GRID_WIDTH && g->player_y < PACMAN_GRID_HEIGHT);
        assert(g->cells[g->player_y][g->player_x] != '#');
        assert(g->lives <= 3u && g->pellets_left <= PACMAN_GRID_WIDTH * PACMAN_GRID_HEIGHT);
        for (unsigned i = 0u; i < PACMAN_GHOST_COUNT; ++i) {
            assert(g->ghosts[i].x < PACMAN_GRID_WIDTH && g->ghosts[i].y < PACMAN_GRID_HEIGHT);
            assert(g->cells[g->ghosts[i].y][g->ghosts[i].x] != '#');
        }
    }
}

int main(void) {
    test_lifecycle();
    test_maze_reachability();
    test_long_random_session();
    puts("pacman_game: lifecycle, maze reachability and random simulation PASS");
    return 0;
}
