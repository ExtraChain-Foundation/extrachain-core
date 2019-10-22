#ifndef BLOWFISH_CRYPT_H
#define BLOWFISH_CRYPT_H
#include <stdint.h>
#include <cstddef>
#include <vector>
#include <cstring>
#include <algorithm>
#include <QByteArray>
class blowFish_crypt
{
public:
    blowFish_crypt();
    std::vector<char> Encrypt(const std::vector<char> &src, const std::vector<char> &key);
    std::vector<char> Decrypt(const std::vector<char> &src, const std::vector<char> &key);
    QByteArray EncryptBlowFish(QByteArray message, QByteArray key);
    QByteArray DecryptBlowFish(QByteArray message, QByteArray key);

private:
    void SetKey(const char *key, size_t byte_length);
    void EncryptBlock(uint32_t *left, uint32_t *right) const;
    void DecryptBlock(uint32_t *left, uint32_t *right) const;
    uint32_t Feistel(uint32_t value) const;

private:
    uint32_t pary_[18];
    uint32_t sbox_[4][256];
};

#endif // BLOWFISH_CRYPT_H
