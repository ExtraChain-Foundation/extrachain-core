#include "extrachainc.h"

#include <string.h>

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *version = extrachain_version();
    printf("ExtraChain version: %s\n", version);
    extrachain_free_char_str(version);
    extrachain_manage_logs(3);

    extrachain_wipe();
    extrachain_init(argc, argv);
    extrachain_auth("1", "1");
    // extrachain_login();

    ActorPrivate *actor_private = extrachain_create_actor(0);
    printf("[ExtraChainC] Actor: %s %d %s %s\n", actor_private->id, actor_private->type,
           actor_private->secret_key, actor_private->public_key);

    ActorPublic *actor_public = extrachain_private_to_public(actor_private);
    if (!extrachain_is_public_actor_valid(actor_public)) {
        printf("[ExtraChainC] Actor %s is not valid %d\n", actor_public->id, actor_public->type);
        return -1;
    }

    printf("[ExtraChainC] Public actor from private: %s %d %s\n", actor_public->id, actor_public->type,
           actor_public->public_key);

    ActorPublic *actor_getted = extrachain_get_actor(actor_public->id);
    printf("[ExtraChainC] After get actor: %s %d %s\n", actor_getted->id, actor_getted->type,
           actor_getted->public_key);

    char *sig = extrachain_sign("EXC", 3, actor_private);
    printf("[ExtraChainC] Sign: %s\n", sig);

    bool verify1 = extrachain_verify_private("EXC", 3, sig, actor_private);
    printf("[ExtraChainC] Sign: %i\n", verify1);

    bool verify2 = extrachain_verify("EXC", 3, sig, actor_getted);
    printf("[ExtraChainC] Sign: %i\n", verify2);

    bool verify3 = extrachain_verify("KEK", 3, sig, actor_getted);
    printf("[ExtraChainC] Sign: %i\n", verify3);
    extrachain_free_char_str(sig);

    const char *encrypted = extrachain_encrypt("EXC", 3, actor_private, actor_getted);
    const char *decrypted = extrachain_decrypt(encrypted, strlen(encrypted), actor_private, actor_getted);
    const char *encrypted_self = extrachain_encrypt_self("EXC", 3, actor_private);
    const char *decrypted_self =
        extrachain_decrypt_self(encrypted_self, strlen(encrypted_self), actor_private);

    printf("[ExtraChainC] Encrypted: %s\n", encrypted);
    printf("[ExtraChainC] Decrypted: %s\n", decrypted);
    printf("[ExtraChainC] Encrypted self: %s\n", encrypted_self);
    printf("[ExtraChainC] Decrypted self: %s\n", decrypted_self);
    extrachain_free_char_str(encrypted);
    extrachain_free_char_str(decrypted);

    extrachain_free_actor_private(actor_private);
    extrachain_free_actor_public(actor_public);
    extrachain_free_actor_public(actor_getted);

    printf("All done\n");
    extrachain_stop();
    so_sleep(10);

    return 0;
}
