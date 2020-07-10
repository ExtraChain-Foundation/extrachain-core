/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// //////////////////////////////////////////////////////////
// keccak.h
// Copyright (c) 2014,2015 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html
//

#pragma once
#include <QByteArray>
//#include "hash.h"
#include <string>

// define fixed size integer types
#ifdef _MSC_VER
// Windows
typedef unsigned __int8 uint8_t;
typedef unsigned __int64 uint64_t;
#else
// GCC
#include <stdint.h>
#endif

/// compute Keccak hash (designated SHA3)
/** Usage:
    Keccak keccak;
    std::string myHash  = keccak("Hello World");     // std::string
    std::string myHash2 = keccak("How are you", 11); // arbitrary data, 11 bytes

    // or in a streaming fashion:

    Keccak keccak;
    while (more data available)
      keccak.add(pointer to fresh data, number of new bytes);
    std::string myHash3 = keccak.getHash();
  */
class Keccak //: public Hash
{
public:
    /// algorithm variants
    enum Bits
    {
        Keccak224 = 224,
        Keccak256 = 256,
        Keccak384 = 384,
        Keccak512 = 512
    };

    /// same as reset()
    explicit Keccak(Bits bits = Keccak256);

    /// compute hash of a memory block
    QByteArray operator()(const void* data, size_t numBytes);
    /// compute hash of a string, excluding final zero
    QByteArray operator()(const QByteArray& text);

    /// add arbitrary number of bytes
    void add(const void* data, size_t numBytes);

    /// return latest hash as hex characters
    QByteArray getHash();

    /// restart
    void reset();

private:
    /// process a full block
    void processBlock(const void* data);
    /// process everything left in the internal buffer
    void processBuffer();

    /// 1600 bits, stored as 25x64 bit, BlockSize is no more than 1152 bits (Keccak224)
    enum
    {
        StateSize = 1600 / (8 * 8),
        MaxBlockSize = 200 - 2 * (224 / 8)
    };

    /// hash
    uint64_t m_hash[StateSize];
    /// size of processed data in bytes
    uint64_t m_numBytes;
    /// block size (less or equal to MaxBlockSize)
    size_t m_blockSize;
    /// valid bytes in m_buffer
    size_t m_bufferSize;
    /// bytes not processed yet
    uint8_t m_buffer[MaxBlockSize];
    /// variant
    Bits m_bits;
};
