#!/bin/sh
set -eu
TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TEST_TMP=$(mktemp -d)
trap 'rm -rf "$TEST_TMP"' EXIT HUP INT TERM
printf '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n' > "$TEST_TMP/Arduino.h"
"${CXX:-c++}" -std=c++11 -I"$TEST_TMP" "$TEST_DIR/navigation.cpp" -o "$TEST_TMP/navigation"
"$TEST_TMP/navigation"
echo 'Navigation checks passed (all provider visibility combinations)'
