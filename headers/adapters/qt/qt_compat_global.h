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

#if defined(EXTRACHAIN_QT_LIBRARY_ACTIVATE)
    #if defined(EXTRACHAIN_QT_LIBRARY)
        #if defined(_WIN32)
            #define EXTRACHAIN_QT_EXPORT __declspec(dllexport)
        #elif defined(__GNUC__) || defined(__clang__)
            #define EXTRACHAIN_QT_EXPORT __attribute__((visibility("default")))
        #else
            #define EXTRACHAIN_QT_EXPORT
        #endif
    #else
        #if defined(_WIN32)
            #define EXTRACHAIN_QT_EXPORT __declspec(dllimport)
        #else
            #define EXTRACHAIN_QT_EXPORT
        #endif
    #endif
#else
    #define EXTRACHAIN_QT_EXPORT
#endif
