/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#if defined __cplusplus
    #include <bitset>
    #include <cassert>
    #include <cctype>
    #include <cerrno>
    #include <cfloat>
    #include <chrono>
    #include <cinttypes>
    #include <climits>
    #include <csetjmp>
    #include <csignal>
    #include <cstdarg>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
    #include <ctime>
    #include <cwchar>
    #include <cwctype>
    #include <exception>
    #include <functional>
    #include <initializer_list>
    #include <limits>
    #include <memory>
    #include <new>
    #include <scoped_allocator>
    #include <stdexcept>
    #include <system_error>
    #include <tuple>
    #include <type_traits>
    #include <typeindex>
    #include <typeinfo>
    #include <utility>
    // #include <cuchar>
    #include <algorithm>
    #include <array>
    #include <cfenv>
    #include <cmath>
    #include <complex>
    #include <deque>
    #include <forward_list>
    #include <fstream>
    #include <ios>
    #include <iosfwd>
    #include <iostream>
    #include <istream>
    #include <iterator>
    #include <list>
    #include <map>
    #include <numeric>
    #include <ostream>
    #include <queue>
    #include <random>
    #include <ratio>
    #include <set>
    #include <sstream>
    #include <stack>
    #include <string>
    #include <unordered_map>
    #include <unordered_set>
    #include <valarray>
    #include <vector>
    // #include <strstream>
    #include <atomic>
    #include <ciso646>
    #include <clocale>
    #include <codecvt>
    #include <condition_variable>
    #include <cstdio>
    #include <future>
    #include <iomanip>
    #include <locale>
    #include <mutex>
    #include <regex>
    #include <streambuf>
    #include <thread>
    // #include <ccomplex>
    // #include <ctgmath>
    // #include <cstdalign>
    // #include <cstdbool>
    #include <optional>
    #include <expected>
    #include <filesystem>
    #include <ranges>
    #include <span>
    #include <concepts>
    #include <source_location>
    #include <coroutine>
    #include <compare>
    #include <version>
    #include <bit>
    #include <numbers>
    #include <barrier>
    #include <latch>
    #include <semaphore>
    #include <charconv>

    #include <sodium.h>
    #include <msgpack.hpp>

    #include <fmt/chrono.h>
    #include <fmt/color.h>
    #include <fmt/core.h>
    #include <fmt/os.h>
    #include <fmt/ostream.h>
    #include <fmt/ranges.h>
    #include <fmt/format.h>

    #include <magic_enum/magic_enum.hpp>
    #include <magic_enum/magic_enum_iostream.hpp>

    #include <boost/generator_iterator.hpp>
    #include <boost/random.hpp>
    #include <boost/core/demangle.hpp>
    #include <boost/algorithm/string/classification.hpp>
    #include <boost/algorithm/string/join.hpp>
    #include <boost/algorithm/string/split.hpp>
    #include <boost/algorithm/string.hpp>
    #include <boost/algorithm/string/replace.hpp>
    #include <boost/interprocess/file_mapping.hpp>
    #include <boost/interprocess/mapped_region.hpp>
    #include <boost/describe.hpp>
    #include <boost/mp11.hpp>
    #include <boost/json.hpp>

    #ifdef _WIN32
        #define UINT32_C(c) (c##ULL)
        #define UINT64_C(c) (c##ULL)
    #endif
#endif
