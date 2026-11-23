#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
"$CC" -std=c11 -Wall -Wextra -Werror -Wpedantic \
  game_shared.c tests/test_game_shared.c -o /tmp/pb10a_game_shared
/tmp/pb10a_game_shared
