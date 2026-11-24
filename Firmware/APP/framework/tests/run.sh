#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
"$CC" -std=c11 -Wall -Wextra -Werror -Wpedantic -no-pie -Itests/stubs -I. -I../../LIB \
    beep_service.c ../../LIB/fifo.c tests/test_fifo_beep.c -o /tmp/pb10a_beep_fifo
/tmp/pb10a_beep_fifo
