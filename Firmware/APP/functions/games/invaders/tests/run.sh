#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
COMMON=(-std=c11 -Wall -Wextra -Werror -Wpedantic -Itests/stubs -I. -I../../../../LIB)
"$CC" "${COMMON[@]}" invaders_assets.c invaders_core.c ../game_shared.c ../../../../LIB/rle.c tests/test_core.c -o /tmp/invaders_host_core
/tmp/invaders_host_core
"$CC" "${COMMON[@]}" invaders_assets.c invaders_core.c ../game_shared.c ../../../../LIB/rle.c game_invaders.c tests/test_adapter.c -o /tmp/invaders_host_adapter
/tmp/invaders_host_adapter
