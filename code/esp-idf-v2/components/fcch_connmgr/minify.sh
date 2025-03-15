#!/bin/bash

# Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
# SPDX-License-Identifier: MIT

# We should do this within CMakeLists.txt, but the esp-idf container doesn't
# have this utility installed:-(

set -e
set -o pipefail

cd -- "$(dirname -- "$0")"

sed -n '/<style>/,/<\/style>/p' < styles-test.html | \
    minify --type css > styles.html
