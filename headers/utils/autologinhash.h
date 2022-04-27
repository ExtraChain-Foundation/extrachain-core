#ifndef AUTOLOGINHASH_H
#define AUTOLOGINHASH_H

#include <string>

// TODO: make specific security

class AutologinHash {
public:
    bool load();
    void save(const std::string &key);
    const std::string &hash() const;

    static bool isAvailable();

private:
    std::string m_hash;
};

#endif // AUTOLOGINHASH_H
