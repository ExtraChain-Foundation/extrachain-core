#include "network/peer_identity.h"

#include "core/byte_array.h"
#include "utils/exc_utils.h"

std::optional<std::string> Network::peer_identifier(const PublicKey& key, std::string_view nonce) {
    if (nonce.size() != 64
        || !std::ranges::all_of(nonce,
                                [](char value) {
                                    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
                                })
        || std::ranges::all_of(key, [](auto value) {
               return value == 0;
           })) {
        return std::nullopt;
    }
    return Utils::calculate_hash("extrachain-node-id-v1:" + ByteArray(key).toString() + std::string(nonce),
                                 Utils::HashAlgorithm::Blake3);
}
