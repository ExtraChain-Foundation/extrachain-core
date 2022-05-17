#ifndef ENC_TOOLS_H
#define ENC_TOOLS_H

#include <string>
#include <vector>

#include <utils/exc_utils.h>

namespace SecretKey {
EXTRACHAIN_EXPORT std::string keygen();
EXTRACHAIN_EXPORT std::string getKeyFromPass(const std::string &pass, const std::string &salt = "");
EXTRACHAIN_EXPORT std::string sign(const std::string &data, const std::string &secret_key);
EXTRACHAIN_EXPORT bool verify(const std::string &data, const std::string &public_key,
                              const std::string &signature);
EXTRACHAIN_EXPORT std::string encrypt(const std::string &msg, const std::string &secret_key);
EXTRACHAIN_EXPORT std::string decrypt(const std::string &msg, const std::string &secret_key);
EXTRACHAIN_EXPORT std::string encryptWithPassword(const std::string &data, const std::string &password);
EXTRACHAIN_EXPORT std::string decryptWithPassword(const std::string &data, const std::string &password);

EXTRACHAIN_EXPORT std::pair<std::string, std::string> createAsymmetricPair();
EXTRACHAIN_EXPORT std::string encryptAsymmetric(const std::string &data, const std::string &secret_key,
                                                const std::string &public_key, const std::string &nonce = "");
EXTRACHAIN_EXPORT std::string decryptAsymmetric(const std::string &data, const std::string &secret_key,
                                                const std::string &public_key, const std::string &nonce = "");
}

#endif // ENC_TOOLS_H
