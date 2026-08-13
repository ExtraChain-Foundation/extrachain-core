/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>

#include "extrachain_global.h"

struct EXTRACHAIN_EXPORT StatusTrx {
    enum StatusTrxType {
        None = -1,
        Approved,
        Processing,
        Failed
    };

    static constexpr StatusTrxType fromInt(int value) noexcept {
        switch (value) {
        case 0:
            return Approved;
        case 1:
            return Processing;
        case 2:
            return Failed;
        default:
            return None;
        }
    }

    static constexpr int toInt(StatusTrxType value) noexcept {
        switch (value) {
        case Approved:
            return 0;
        case Processing:
            return 1;
        case Failed:
            return 2;
        case None:
            return -1;
        }
        return -1;
    }

    static std::string toString(int value) {
        switch (value) {
        case 0:
            return "Approved";
        case 1:
            return "Processing";
        case 2:
            return "Failed";
        case None:
            return "-1";
        default:
            return {};
        }
    }
};
