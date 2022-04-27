#ifndef EXTRACHAINC_H
#define EXTRACHAINC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdio.h>

void so_sleep(unsigned long msecs);

typedef struct {
    char *id;
    char *secret_key;
    char *public_key;
    int type;
} ActorPrivate;
typedef struct {
    char *id;
    char *public_key;
    int type;
} ActorPublic;

void *extrachain_node_pointer();
void extrachain_free_char_str(const char *str);
void extrachain_free_actor_private(ActorPrivate *actor_private);
void extrachain_free_actor_public(ActorPublic *actor_public);

char *extrachain_version();
void extrachain_wipe();
void extrachain_manage_logs(int log_type);

void extrachain_init(int argc, char *argv[]);
void extrachain_auth(char *login, char *password);
void extrachain_login();
void extrachain_stop();

ActorPrivate *extrachain_create_actor(int type);
ActorPublic *extrachain_get_actor(char actor_id[20]);
ActorPublic *extrachain_private_to_public(ActorPrivate *actor_private);
bool extrachain_is_public_actor_valid(ActorPublic *actor_public);

char *extrachain_sign(const char *data, size_t size, const ActorPrivate *actor_private);
bool extrachain_verify_private(const char *data, size_t size, const char *sign,
                               const ActorPrivate *actor_public);
bool extrachain_verify(const char *data, size_t size, const char *sign, const ActorPublic *actor_public);

char *extrachain_encrypt(const char *data, size_t size, const ActorPrivate *actor_private,
                         const ActorPublic *actor_public);
char *extrachain_decrypt(const char *data, size_t size, const ActorPrivate *actor_private,
                         const ActorPublic *actor_public);
char *extrachain_encrypt_self(const char *data, size_t size, const ActorPrivate *actor_private);
char *extrachain_decrypt_self(const char *data, size_t size, const ActorPrivate *actor_private);

void extrachain_network_connect(const char *ip, int type);   // type: 1 - udp, 2 - ws
void extrachain_network_send(const char *data, size_t size); // TODO: package num

// void extrachain_create_profile

// void extrachain_dfs_new_file
// char* extrachain_dfs_get_file

// extrachain_blockchain_get_block
// extrachain_blockchain_get_transaction
// extrachain_blockchain_get_wallet_sum

#ifdef __cplusplus
}
#endif

#endif // EXTRACHAINC_H
