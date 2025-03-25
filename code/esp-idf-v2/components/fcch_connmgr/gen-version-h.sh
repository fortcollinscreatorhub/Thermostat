#!/bin/bash

set -e

version_h="$1"

version="$(git describe --always --tags --dirty)"

regen=0
if [[ "${version}" =~ dirty ]]; then
    regen=1
elif [ ! -f "${version_h}" ]; then
    regen=1
else
    cur_version="$(grep "APP_VERSION" "${version_h}"|cut -d\" -f 2)"
    if [ "${cur_version}" != "${version}" ]; then
        regen=1
    fi
fi

if [ ${regen} -eq 0 ]; then
    exit 0
fi

cat > "${version_h}" <<END_HERE
#pragma once
#define APP_VERSION "${version}"
#define APP_TIMESTAMP "$(date)"
END_HERE
