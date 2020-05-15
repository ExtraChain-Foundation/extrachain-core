#ifndef BLOWFISH_CRYPT_H
#define BLOWFISH_CRYPT_H
#include <stdint.h>
#include <cstddef>
#include <vector>
#include <cstring>
#include <algorithm>
#include <QByteArray>

class BlowFish
{
public:
    static QByteArray encrypt(QByteArray message, QByteArray key);
    static QByteArray decrypt(QByteArray message, QByteArray key);

private:
    BlowFish();
    static void SetKey(const char *key, size_t byte_length, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static void EncryptBlock(uint32_t *left, uint32_t *right, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static void DecryptBlock(uint32_t *left, uint32_t *right, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static std::vector<char> Encrypt(const std::vector<char> &src, const std::vector<char> &key,
                                     uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static std::vector<char> Decrypt(const std::vector<char> &src, const std::vector<char> &key,
                                     uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static uint32_t Feistel(uint32_t value, uint32_t (&sbox)[4][256]);
};

#endif // BLOWFISH_CRYPT_H
