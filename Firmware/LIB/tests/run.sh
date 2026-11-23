#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for cc in gcc clang; do
    command -v "$cc" >/dev/null || continue
    "$cc" -std=c99 -Wall -Wextra -Werror -Wpedantic -O2 -I. rle.c fifo.c tests/test_lib.c -o "/tmp/pb10a_lib_$cc"
    "/tmp/pb10a_lib_$cc"
done
if command -v gcc >/dev/null; then
    gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -I. rle.c fifo.c tests/test_lib.c -o /tmp/pb10a_lib_asan
    ASAN_OPTIONS=detect_leaks=0 /tmp/pb10a_lib_asan
fi
