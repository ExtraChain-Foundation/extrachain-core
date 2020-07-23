#ifndef ENC_TOOLS_H
#define ENC_TOOLS_H

#include <sodium.h>
#include <vector>
#include <string>
#include <utils/exc_utils.h>

using namespace std;
namespace SecretKey {
string keygen();
string getKeyFromPass(const string &pass, const string &salt = "");
string encrypt(const string &msg, const string &secret_key);
string decrypt(const string &msg, const string &secret_key);
string encryptWithPassword(const string &data, const string &password);
string decryptWithPassword(const string &data, const string &password);
}
#endif // ENC_TOOLS_H
