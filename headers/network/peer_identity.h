#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "encryption/encryption_tools.h"

namespace Network {
    EXTRACHAIN_EXPORT std::optional<std::string> peer_identifier(const PublicKey& key, std::string_view nonce);
}
