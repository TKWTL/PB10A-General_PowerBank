# Pac-Man host tests

Only the production C sources (`game_pacman.c` and `game_pacman_core.c`)
are included in the Keil project. Run these host tests from
`Firmware/APP/functions/`:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Ipacman_tests/stubs -I. game_pacman_core.c pacman_tests/test_pacman_game.c -o /tmp/pacman_game
/tmp/pacman_game
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Ipacman_tests/stubs -I. game_pacman_core.c game_pacman.c pacman_tests/test_pacman_app.c -o /tmp/pacman_app
/tmp/pacman_app
```

The adapter intentionally does not set a PM sleep-block bit: the system
retains normal automatic sleep policy. The tests do not constitute an
ARMClang build or device/display verification.
