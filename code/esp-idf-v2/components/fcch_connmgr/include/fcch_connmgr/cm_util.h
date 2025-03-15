// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include <type_traits>

#define ARRAY_SIZE(x) ((sizeof (x)) / (sizeof ((x)[0])))

extern uint8_t cm_util_hex_char_to_uint(unsigned char c);
extern unsigned char cm_util_uint_to_hex_char(uint8_t val);

template<typename T> struct AutoFree {
    using Tnc = std::remove_const_t<T>;

    AutoFree() {}
    AutoFree(T* val) : val(val) {}
    ~AutoFree() {
        free(const_cast<Tnc *>(val));
    }
    T* val = nullptr;
};

template<typename T> struct AutoCleanup {
    using cleanup_t = void (*)(T& val);

    AutoCleanup(cleanup_t cleanup, T& val) :
        cleanup(cleanup), val(val) {}
    ~AutoCleanup() {
        cleanup(val);
    }
    cleanup_t cleanup;
    T& val;
};
