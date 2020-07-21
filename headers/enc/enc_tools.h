#ifndef ENC_TOOLS_H
#define ENC_TOOLS_H

#include <sodium.h>
#include <vector>
#include <string>
#include <utils/exc_utils.h>

using namespace std;
namespace SecretKey {
string keygen();
string getKeyFromPass(string pass, string salt = "");
string encrypt(string msg, string &secret_key);
string decrypt(string msg, string &secret_key);
string encryptWithPassword(string data, string password);
string decryptWithPassword(string data, string password);
}
#endif // ENC_TOOLS_H
