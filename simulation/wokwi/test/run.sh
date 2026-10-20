#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin="$(mktemp "${TMPDIR:-/tmp}/splitflap-rotor.XXXXXX")"
trap 'rm -f "$bin"' EXIT
"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -pedantic -I "$here" "$here/test_rotor.c" -lm -o "$bin"
"$bin"
