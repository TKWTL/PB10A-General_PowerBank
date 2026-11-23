#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
COMMON=(-std=c11 -Wall -Wextra -Werror -Wpedantic -Itests/stubs -I.)
"$CC" "${COMMON[@]}" bomber_assets.c bomber_core.c tests/test_core.c -o /tmp/bomber_host_core
/tmp/bomber_host_core
"$CC" "${COMMON[@]}" bomber_assets.c bomber_core.c game_bomber.c tests/test_adapter.c -o /tmp/bomber_host_adapter
/tmp/bomber_host_adapter
