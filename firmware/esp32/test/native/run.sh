#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/splitflap-native.XXXXXX")"
trap 'rm -rf "$build"' EXIT

# No Arduino, hardware, PlatformIO or network dependencies are needed.
# SANITIZE=1 enables address/undefined-behaviour checks on supported hosts.
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -O2)
if [[ "${SANITIZE:-0}" == "1" ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
"${CXX:-g++}" "${flags[@]}" -I "$root/include" \
  "$root/src/motion.cpp" "$root/test/native/test_motion.cpp" \
  -o "$build/test_motion"
"$build/test_motion"

for name in bus characters; do
  "${CXX:-g++}" "${flags[@]}" -I "$root/include" \
    "$root/test/native/test_${name}.cpp" -o "$build/test_${name}"
  "$build/test_${name}"
done
"${CXX:-g++}" "${flags[@]}" -I "$root/test/native/adapter_stubs" -I "$root/include" \
  "$root/src/motion.cpp" "$root/test/native/test_adapter.cpp" -o "$build/test_adapter"
"$build/test_adapter"

"${CXX:-g++}" "${flags[@]}" -I "$root/test/native/adapter_stubs" -I "$root/include" \
  "$root/src/motion.cpp" "$root/test/native/test_closed_loop.cpp" -o "$build/test_closed_loop"
"$build/test_closed_loop"
