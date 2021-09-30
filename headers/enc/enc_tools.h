#ifndef ENC_TOOLS_H
#define ENC_TOOLS_H

#include <sodium.h>
#include <string>
#include <vector>

#include <utils/exc_utils.h>

namespace SecretKey {
std::string keygen();
std::string getKeyFromPass(const std::string &pass, const std::string &salt = "");
std::string encrypt(const std::string &msg, const std::string &secret_key);
std::string decrypt(const std::string &msg, const std::string &secret_key);
std::string encryptWithPassword(const std::string &data, const std::string &password);
std::string decryptWithPassword(const std::string &data, const std::string &password);

std::pair<std::string, std::string> createAsymmetricPair();
std::string encryptAsymmetric(const std::string &data, const std::string &secret_key,
                              const std::string &public_key, const std::string &nonce = "");
std::string decryptAsymmetric(const std::string &data, const std::string &secret_key,
                              const std::string &public_key, const std::string &nonce = "");

QByteArray encryptAsymmetric(const QByteArray &data, const QByteArray &secret_key,
                             const QByteArray &public_key, const QByteArray &nonce = "");
QByteArray decryptAsymmetric(const QByteArray &data, const QByteArray &secret_key,
                             const QByteArray &public_key, const QByteArray &nonce = "");
}
#endif // ENC_TOOLS_H
