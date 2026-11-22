#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
COMMON=(-std=c11 -Wall -Wextra -Werror -Wpedantic -Itests/stubs -I.)
"$CC" "${COMMON[@]}" pacman_core.c pacman_assets.c tests/test_core.c -o /tmp/pacman_v2_core
/tmp/pacman_v2_core
"$CC" "${COMMON[@]}" pacman_core.c pacman_assets.c tests/test_adapter.c -o /tmp/pacman_v2_adapter
/tmp/pacman_v2_adapter
