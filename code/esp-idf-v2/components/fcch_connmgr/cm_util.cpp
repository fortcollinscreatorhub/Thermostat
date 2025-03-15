// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include "fcch_connmgr/cm_util.h"

uint8_t cm_util_hex_char_to_uint(unsigned char c) {
    if ((c >= '0') && (c <= '9'))
        return c - '0';
    if ((c >= 'A') && (c <= 'Z'))
        return c - 'A' + 10;
    if ((c >= 'a') && (c <= 'z'))
        return c - 'a' + 10;
    return 0;
}

unsigned char cm_util_uint_to_hex_char(uint8_t val) {
    if (val < 10)
        return '0' + val;
    else if (val < 16)
        return 'A' + val - 10;
    else
        return '?';
}
