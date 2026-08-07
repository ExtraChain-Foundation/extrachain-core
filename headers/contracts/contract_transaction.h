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

#include <boost/describe/class.hpp>

#include <cstdint>
#include <string>

struct ContractTransactionData {
    std::uint32_t schema = 2;
    std::string   kind;
    std::string   method;
    std::string   arguments_base64;
    std::string   module_hash;
    std::string   previous_state_hash;
    std::string   state_hash;
    std::uint32_t version             = 1;
    std::uint64_t revision            = 1;
    bool          checkpoint          = false;
    std::uint64_t checkpoint_revision = 0;
};
BOOST_DESCRIBE_STRUCT(ContractTransactionData,
                      (),
                      (schema,
                       kind,
                       method,
                       arguments_base64,
                       module_hash,
                       previous_state_hash,
                       state_hash,
                       version,
                       revision,
                       checkpoint,
                       checkpoint_revision))
