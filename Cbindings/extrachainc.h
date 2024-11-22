/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

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
    int   type;
} ActorPrivate;
typedef struct {
    char *id;
    char *public_key;
    int   type;
} ActorPublic;

void *extrachain_node_pointer();
void  extrachain_free_char_str(const char *str);
void  extrachain_free_actor_private(ActorPrivate *actor_private);
void  extrachain_free_actor_public(ActorPublic *actor_public);

char *extrachain_version();
void  extrachain_wipe();
void  extrachain_manage_logs(int log_type);

void extrachain_init(int argc, char *argv[]);
void extrachain_auth(char *login, char *password);
void extrachain_login();
void extrachain_stop();

ActorPrivate *extrachain_create_actor(int type);
ActorPublic  *extrachain_get_actor(char actor_id[20]);
ActorPublic  *extrachain_private_to_public(ActorPrivate *actor_private);
bool          extrachain_is_public_actor_valid(ActorPublic *actor_public);

char *extrachain_sign(const char *data, size_t size, const ActorPrivate *actor_private);
bool  extrachain_verify_private(const char *data, size_t size, const char *sign, const ActorPrivate *actor_public);
bool  extrachain_verify(const char *data, size_t size, const char *sign, const ActorPublic *actor_public);

char *extrachain_encrypt(const char         *data,
                         size_t              size,
                         const ActorPrivate *actor_private,
                         const ActorPublic  *actor_public);
char *extrachain_decrypt(const char         *data,
                         size_t              size,
                         const ActorPrivate *actor_private,
                         const ActorPublic  *actor_public);
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
