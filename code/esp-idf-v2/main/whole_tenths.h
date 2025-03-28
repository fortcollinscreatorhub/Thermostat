// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

struct Whole_Tenths {
    explicit Whole_Tenths(float f) {
        uint32_t times_10 = (uint32_t)(f * 10);
        whole = times_10 / 10;
        tenths = times_10 % 10;
    }

    uint32_t whole;
    uint32_t tenths;
};
