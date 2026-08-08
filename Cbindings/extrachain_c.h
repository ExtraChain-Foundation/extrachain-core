/*
 * ExtraChain Core — C FFI Public API
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Single public header — include only this file.
 * All functions are thread-safe (dispatched to the Qt event loop internally).
 * Strings returned by exc_* functions must be freed via exc_string_free().
 * Handles must be freed via the matching exc_*_free() or exc_handle_free().
 */

#ifndef EXTRACHAIN_C_H
#define EXTRACHAIN_C_H

#include "extrachain_c_types.h"

#define EXC_C_API_VERSION 2u

#ifdef _WIN32
#ifdef EXTRACHAIN_C_BUILDING
#define EXC_API __declspec(dllexport)
#else
#define EXC_API __declspec(dllimport)
#endif
#else
#define EXC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ════════════════════════════════════════════════════════════════════
 *  Node lifecycle
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Initialise the node on a background thread.
 * Creates QCoreApplication and starts the Qt event loop internally.
 * ws_port: WebSocket listen port (0 = default 17593).
 * Returns EXC_OK or EXC_ERR_ALREADY_INITIALIZED.
 */
EXC_API ExcError exc_init(int argc, char** argv, uint16_t ws_port);

/*
 * Initialise the node but do NOT start the event loop.
 * The caller must call exc_run() on the main thread afterwards.
 * Useful on macOS where the main thread must own the run loop.
 */
EXC_API ExcError exc_init_main_thread(int argc, char** argv, uint16_t ws_port);

/*
 * Blocking: run the Qt event loop on the calling thread.
 * Only valid after exc_init_main_thread(). Returns after exc_shutdown().
 */
EXC_API ExcError exc_run(void);

/* Returns true after a successful exc_init / exc_init_main_thread. */
EXC_API bool exc_is_initialized(void);

/* Returns the library version string. Caller must NOT free the result. */
EXC_API const char* exc_version(void);

/* Returns the public C API revision used by this header and library. */
EXC_API uint32_t exc_api_version(void);

/* Configure logging. log_type maps to internal eLog levels. */
EXC_API ExcError exc_configure_logs(int log_type);

/* Login with plaintext credentials. The hash is computed internally. */
EXC_API ExcError exc_login(const char* login, const char* password);

/* Login with a pre-computed hash (hex string). */
EXC_API ExcError exc_login_hash(const char* hash);

/* Logout the current profile. */
EXC_API ExcError exc_logout(void);

/* Gracefully shut down the node and free resources. */
EXC_API ExcError exc_shutdown(void);

/* Delete all local data (blockchain, DFS, profiles). */
EXC_API ExcError exc_wipe_data(void);

/* ════════════════════════════════════════════════════════════════════
 *  Memory management
 * ════════════════════════════════════════════════════════════════════ */

EXC_API void exc_string_free(char* str);
EXC_API void exc_bytes_free(ExcBytes* bytes);
EXC_API void exc_handle_free(ExcHandle handle);
EXC_API void exc_mnemonic_free(ExcMnemonic* mnemonic);
EXC_API void exc_balance_list_free(ExcBalanceList* list);
EXC_API void exc_string_list_free(ExcStringList* list);

/* ════════════════════════════════════════════════════════════════════
 *  Account management
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Create a new profile (BIP39). Automatically generates a seed phrase.
 * login/password are used to encrypt the profile on disk.
 * On success, fills out_mnemonic (caller frees via exc_mnemonic_free).
 */
EXC_API ExcError exc_profile_create(const char* login, const char* password,
                                    ExcMnemonic* out_mnemonic);

/*
 * Import a profile from a BIP39 seed phrase.
 * Returns the main ActorId in out_main_id (caller frees via exc_string_free).
 */
EXC_API ExcError exc_profile_import_seed(const char* login, const char* password,
                                         const char* phrase, char** out_main_id);

/*
 * Import a profile from exported data (JSON string from exc_profile_export).
 * Returns the main ActorId in out_main_id (caller frees via exc_string_free).
 */
EXC_API ExcError exc_profile_import_data(const char* data, const char* login,
                                         const char* password, char** out_main_id);

/*
 * Import a profile from an exported file.
 * Returns the main ActorId in out_main_id (caller frees via exc_string_free).
 */
EXC_API ExcError exc_profile_import_file(const char* file_path, const char* login,
                                         const char* password, char** out_main_id);

/*
 * Export the current profile as a JSON string.
 * Caller frees the result via exc_string_free.
 */
EXC_API ExcError exc_profile_export(char** out_data);

/* Create a new wallet under the current profile. Returns ActorId. */
EXC_API ExcError exc_wallet_create(char** out_wallet_id);

/* Get the current wallet's ActorId. Caller frees via exc_string_free. */
EXC_API ExcError exc_wallet_current_id(char** out_wallet_id);

/* List all wallet ActorIds. Caller frees via exc_string_list_free. */
EXC_API ExcError exc_wallet_list(ExcStringList* out_list);

/* Get the main (first) wallet ActorId. Caller frees via exc_string_free. */
EXC_API ExcError exc_profile_main_id(char** out_main_id);

/* Validate a BIP39 mnemonic phrase. */
EXC_API bool exc_mnemonic_validate(const char* phrase);

/* Get the current profile type (Old/New). */
EXC_API ExcError exc_profile_type(ExcProfileType* out_type);

/* Get the seed phrase of the current (New) profile. Caller frees via exc_string_free. */
EXC_API ExcError exc_profile_seed_phrase(char** out_phrase);

/* ════════════════════════════════════════════════════════════════════
 *  Transactions & Balance (DAG)
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Send a transaction from the current wallet to receiver.
 * amount: decimal string (e.g. "100.5").
 * token_id: token ActorId, or NULL/empty for native token.
 * On success, out_tx_handle receives a Transaction handle.
 */
EXC_API ExcError exc_transaction_send(const char* receiver, const char* amount,
                                      const char* token_id, ExcHandle* out_tx_handle);

/*
 * Send a transaction from a specific sender wallet.
 * sender: ActorId of the sending wallet.
 */
EXC_API ExcError exc_transaction_send_from(const char* sender, const char* receiver,
                                           const char* amount, const char* token_id,
                                           ExcHandle* out_tx_handle);

/* Query balance for an actor and token. Returns decimal string. */
EXC_API ExcError exc_balance_query(const char* actor_id, const char* token_id,
                                   char** out_balance);

/* Query all balances for a set of actors. */
EXC_API ExcError exc_balance_query_all(const char** actor_ids, size_t count,
                                       ExcBalanceList* out_balances);

/* ── Transaction accessors (from handle) ─────────────────────────── */

EXC_API ExcError exc_transaction_get_sender(ExcHandle tx, char** out);
EXC_API ExcError exc_transaction_get_receiver(ExcHandle tx, char** out);
EXC_API ExcError exc_transaction_get_amount(ExcHandle tx, char** out);
EXC_API ExcError exc_transaction_get_hash(ExcHandle tx, char** out);
EXC_API ExcError exc_transaction_get_section(ExcHandle tx, char** out);
EXC_API ExcError exc_transaction_get_type(ExcHandle tx, ExcTransactionType* out);
EXC_API ExcError exc_transaction_get_timestamp(ExcHandle tx, uint64_t* out);
EXC_API ExcError exc_transaction_get_token(ExcHandle tx, char** out);
EXC_API void     exc_transaction_free(ExcHandle tx);

/* ── DAG queries ─────────────────────────────────────────────────── */

EXC_API ExcError exc_dag_current_section(char** out_section);
EXC_API ExcError exc_dag_status(ExcDagStatus* out_status);
EXC_API ExcError exc_dag_mode(ExcDagMode* out_mode);

/* Set DAG mode (force_full_mode or force_light_mode). */
EXC_API ExcError exc_dag_set_mode(ExcDagMode mode);

/* Read a section by ID. Returns JSON string. Caller frees via exc_string_free. */
EXC_API ExcError exc_dag_read_section(const char* section_id, char** out_json);

/* Look up a transaction by (section_id, hash). Returns a Transaction handle. */
EXC_API ExcError exc_dag_find_transaction(const char* section_id,
                                          const char* hash,
                                          ExcHandle*  out_tx);

/* Free a section handle (from exc_dag_read_section if handle-based). */
EXC_API void exc_section_free(ExcHandle section);

/* ════════════════════════════════════════════════════════════════════
 *  Mining
 * ════════════════════════════════════════════════════════════════════ */

/* Start mining (switches DAG to Full mode, starts reward timer). */
EXC_API ExcError exc_mining_start(void);

/* Stop mining (stops reward timer, switches DAG to Light mode). */
EXC_API ExcError exc_mining_stop(void);

/* Check if mining is active (DAG mode is Full). */
EXC_API ExcError exc_mining_is_active(bool* out_active);

/* ════════════════════════════════════════════════════════════════════
 *  Cryptography (standalone, no node required after init)
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Generate a new keypair. Returns handles for private/public actors.
 * out_secret_key, out_public_key: hex-encoded strings.
 * out_actor_id: Blake3 hash of public key (40 chars).
 * Caller frees all strings via exc_string_free.
 */
EXC_API ExcError exc_crypto_generate_keypair(char** out_actor_id,
                                             char** out_secret_key,
                                             char** out_public_key);

/* Sign data with a private key. Returns hex-encoded signature. */
EXC_API ExcError exc_crypto_sign(const uint8_t* data, size_t data_len,
                                 const char* secret_key_hex, const char* public_key_hex,
                                 char** out_signature_hex);

/* Verify a signature with a public key. */
EXC_API ExcError exc_crypto_verify(const uint8_t* data, size_t data_len,
                                   const char* signature_hex, const char* public_key_hex,
                                   bool* out_valid);

/* Asymmetric encrypt: sender_secret + receiver_public. */
EXC_API ExcError exc_crypto_encrypt(const uint8_t* data, size_t data_len,
                                    const char* sender_secret_hex,
                                    const char* receiver_public_hex,
                                    ExcBytes* out_encrypted);

/* Asymmetric decrypt: receiver_secret + sender_public. */
EXC_API ExcError exc_crypto_decrypt(const uint8_t* data, size_t data_len,
                                    const char* receiver_secret_hex,
                                    const char* sender_public_hex,
                                    ExcBytes* out_decrypted);

/* Self-encrypt (same key for encrypt/decrypt). */
EXC_API ExcError exc_crypto_encrypt_self(const uint8_t* data, size_t data_len,
                                         const char* secret_key_hex,
                                         const char* public_key_hex,
                                         ExcBytes* out_encrypted);

/* Self-decrypt. */
EXC_API ExcError exc_crypto_decrypt_self(const uint8_t* data, size_t data_len,
                                         const char* secret_key_hex,
                                         const char* public_key_hex,
                                         ExcBytes* out_decrypted);

/* ════════════════════════════════════════════════════════════════════
 *  DFS (Distributed File System)
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Store a local file into DFS.
 * owner_id: ActorId of the owner.
 * file_path: local filesystem path.
 * visual_folder: display folder path (e.g. "/photos").
 * visual_name: display name.
 * security: data security mode.
 * On success, out_dir_row_json is a JSON-encoded DirRow.
 */
EXC_API ExcError exc_dfs_store_file(const char* owner_id, const char* file_path,
                                    const char* visual_folder, const char* visual_name,
                                    ExcDataSecurity security, char** out_dir_row_json);

/* Export a DFS file to a local path. */
EXC_API ExcError exc_dfs_export_file(const char* owner_id, const char* file_id,
                                     const char* output_folder);

/* Store a new vector (structured data container). */
EXC_API ExcError exc_dfs_store_vector(const char* owner_id, const char* visual_name,
                                      const char* template_owner_id,
                                      const char* template_file_id,
                                      ExcDataSecurity security, char** out_dir_row_json);

/* Add a row to a vector (JSON object). */
EXC_API ExcError exc_dfs_vector_add_row(const char* owner_id, const char* file_id,
                                        const char* row_json);

/* Update a row in a vector (JSON object with matching primary key). */
EXC_API ExcError exc_dfs_vector_update_row(const char* owner_id, const char* file_id,
                                           const char* row_json);

/* Remove a row from a vector by primary key. */
EXC_API ExcError exc_dfs_vector_remove_row(const char* owner_id, const char* file_id,
                                           const char* primary_data);

/*
 * Read all rows from a vector. Returns JSON array.
 * where_clause: optional SQL WHERE clause (can be NULL).
 * Caller frees out_json via exc_string_free.
 */
EXC_API ExcError exc_dfs_vector_read_rows(const char* owner_id, const char* file_id,
                                          const char* where_clause, char** out_json);

/* Store a dictionary (key-value store). */
EXC_API ExcError exc_dfs_store_dictionary(const char* owner_id, const char* visual_name,
                                          ExcDataSecurity security,
                                          char** out_dir_row_json);

/* Set a value in a dictionary. */
EXC_API ExcError exc_dfs_dictionary_set(const char* owner_id, const char* file_id,
                                        const char* key, const char* value);

/* Get a value from a dictionary. Caller frees via exc_string_free. */
EXC_API ExcError exc_dfs_dictionary_get(const char* owner_id, const char* file_id,
                                        const char* key, char** out_value);

/* Remove a key from a dictionary. */
EXC_API ExcError exc_dfs_dictionary_remove(const char* owner_id, const char* file_id,
                                           const char* key);

/* List all user files (skipping system folders). Returns JSON array. */
EXC_API ExcError exc_dfs_list_files(const char* owner_id, char** out_json);

/* Remove a stored file. */
EXC_API ExcError exc_dfs_remove_file(const char* owner_id, const char* file_id);

/* Read a DirRow by file_id. Returns JSON. */
EXC_API ExcError exc_dfs_read_dir_row(const char* owner_id, const char* file_id,
                                      char** out_json);

/* Request a file download from the network. */
EXC_API ExcError exc_dfs_request_file(const char* owner_id, const char* file_id);

/* Get DFS usage stats. */
EXC_API ExcError exc_dfs_size_taken(uint64_t* out_bytes);
EXC_API ExcError exc_dfs_size_limit(uint64_t* out_bytes);
EXC_API ExcError exc_dfs_size_available(uint64_t* out_bytes);

/* ════════════════════════════════════════════════════════════════════
 *  Network
 * ════════════════════════════════════════════════════════════════════ */

/* Connect to a peer by IP (WebSocket). */
EXC_API ExcError exc_network_connect(const char* ip);

/* Start the network server. */
EXC_API ExcError exc_network_start(void);

/* Check if any connections are active. */
EXC_API ExcError exc_network_is_connected(bool* out_connected);

/* Get count of active connections. */
EXC_API ExcError exc_network_connection_count(int* out_count);

/* Get the node's public IP. Caller frees via exc_string_free. */
EXC_API ExcError exc_network_public_ip(char** out_ip);

/* Get the node's local IP. Caller frees via exc_string_free. */
EXC_API ExcError exc_network_local_ip(char** out_ip);

/* ════════════════════════════════════════════════════════════════════
 *  Chat
 * ════════════════════════════════════════════════════════════════════ */

/* Create a dialogue with another actor. Returns JSON-encoded Chat::Chat. */
EXC_API ExcError exc_chat_create_dialogue(const char* with_actor_id, char** out_chat_json);

/* Create a "self" chat (notes). Returns JSON. */
EXC_API ExcError exc_chat_create_myself(char** out_chat_json);

/* List all chats. Returns JSON array. Caller frees via exc_string_free. */
EXC_API ExcError exc_chat_list(char** out_json);

/* Send a text message to a chat. */
EXC_API ExcError exc_chat_send_text(const char* owner_id, const char* file_id,
                                    const char* text, const char* reply_id);

/* Read messages from a chat. Returns JSON array. Caller frees via exc_string_free. */
EXC_API ExcError exc_chat_read_messages(const char* owner_id, const char* file_id,
                                        char** out_json);

/* Read the last message from a chat. Returns JSON. */
EXC_API ExcError exc_chat_read_last_message(const char* owner_id, const char* file_id,
                                            char** out_json);

/* Remove a message by ID. */
EXC_API ExcError exc_chat_remove_message(const char* owner_id, const char* file_id,
                                         const char* message_id);

/* ════════════════════════════════════════════════════════════════════
 *  Janus (Marketplace)
 * ════════════════════════════════════════════════════════════════════ */

/* Create the default bid template ("JanusBids"). */
EXC_API ExcError exc_janus_create_default_bid_template(const char* template_name);

/* Create an item vector for a marketplace. */
EXC_API ExcError exc_janus_create_item_vector(const char* vector_name,
                                              const char* template_owner_id,
                                              const char* bid_template_name,
                                              char** out_dir_row_json);

/* Place a bid on an item (JSON-encoded bid struct). */
EXC_API ExcError exc_janus_place_bid(const char* item_owner_id,
                                     const char* item_file_id,
                                     const char* bid_json);

/* ════════════════════════════════════════════════════════════════════
 *  Tokens
 * ════════════════════════════════════════════════════════════════════ */

/*
 * Create a custom token.
 * amount: decimal string of initial supply.
 * color: hex color string (e.g. "#FF0000").
 * On success, out_token_json is JSON-encoded TokenData.
 */
EXC_API ExcError exc_token_create(const char* name,
                                  const char* ticker,
                                  const char* amount,
                                  uint8_t     decimals,
                                  const char* color,
                                  char**      out_token_json);

/* Check if a token with given name and ticker exists. */
EXC_API ExcError exc_token_exists(const char* name, const char* ticker, bool* out_exists);

/* Read all known tokens. Returns JSON object {token_id: name, ...}. */
EXC_API ExcError exc_token_list(char** out_json);

/* Read legacy tokens that can be converted to standard contracts. Full nodes only. */
EXC_API ExcError exc_token_legacy_list(char** out_json);

/* Convert one legacy token in place. The current profile must contain the owner key. */
EXC_API ExcError exc_token_migrate(const char* token_id, char** out_token_json);

/* ════════════════════════════════════════════════════════════════════
 *  WebAssembly contracts
 * ════════════════════════════════════════════════════════════════════ */

/* Deploy a module. init_arguments contains MessagePack data. */
EXC_API ExcError exc_contract_deploy(const char*    kind,
                                     const uint8_t* module,
                                     size_t         module_len,
                                     const uint8_t* init_arguments,
                                     size_t         init_arguments_len,
                                     ExcHandle*     out_tx_handle);

/* Submit a state-changing call. arguments contains MessagePack data. */
EXC_API ExcError exc_contract_call(const char*    contract_id,
                                   const char*    method,
                                   const uint8_t* arguments,
                                   size_t         arguments_len,
                                   ExcHandle*     out_tx_handle);

/* Run a read-only method and return its MessagePack result as URL-safe Base64. */
EXC_API ExcError exc_contract_query(const char*    contract_id,
                                    const char*    method,
                                    const uint8_t* arguments,
                                    size_t         arguments_len,
                                    char**         out_result_base64);

/* Submit an owner-approved immutable module upgrade. */
EXC_API ExcError exc_contract_upgrade(const char*    contract_id,
                                      const uint8_t* module,
                                      size_t         module_len,
                                      const uint8_t* migration_arguments,
                                      size_t         migration_arguments_len,
                                      ExcHandle*     out_tx_handle);

/* Return identity, version, revision, and current hashes as JSON. */
EXC_API ExcError exc_contract_inspect(const char* contract_id, char** out_json);

/* Return the trusted contract component catalog as JSON. Desktop use only. */
EXC_API ExcError exc_contract_components(char** out_json);

/* Generate Rust source from a JSON array of component IDs. Desktop use only. */
EXC_API ExcError exc_contract_compose(const char* project_name, const char* component_ids_json, char** out_source);

/* ════════════════════════════════════════════════════════════════════
 *  Callbacks (event registration)
 * ════════════════════════════════════════════════════════════════════ */

/* Node */
EXC_API void exc_on_node_ready(ExcNodeReadyCallback cb, ExcUserData user_data);

/* DAG sync */
EXC_API void exc_on_dag_sync_start(ExcDagSyncStartCallback cb, ExcUserData user_data);
EXC_API void exc_on_dag_sync_progress(ExcDagSyncProgressCallback cb, ExcUserData user_data);
EXC_API void exc_on_dag_sync_finished(ExcDagSyncFinishCallback cb, ExcUserData user_data);
EXC_API void exc_on_dag_status(ExcDagStatusCallback cb, ExcUserData user_data);

/* DAG transactions */
EXC_API void exc_on_dag_tx_sended(ExcDagTxSendedCallback cb, ExcUserData user_data);
EXC_API void exc_on_dag_tx_approved(ExcDagTxApprovedCallback cb, ExcUserData user_data);
EXC_API void exc_on_dag_tx_not_approved(ExcDagTxNotApprovedCallback cb, ExcUserData user_data);
EXC_API void exc_on_self_tx(ExcSelfTxCallback cb, ExcUserData user_data);

/* Chat */
EXC_API void exc_on_chat_message(ExcChatMessageCallback cb, ExcUserData user_data);
EXC_API void exc_on_chats_loaded(ExcChatsLoadedCallback cb, ExcUserData user_data);
EXC_API void exc_on_chat_added(ExcChatAddedCallback cb, ExcUserData user_data);

/* DFS */
EXC_API void exc_on_dfs_stored(ExcDfsStoredCallback cb, ExcUserData user_data);
EXC_API void exc_on_dfs_downloaded(ExcDfsDownloadedCallback cb, ExcUserData user_data);
EXC_API void exc_on_dfs_download_progress(ExcDfsDownloadProgressCallback cb, ExcUserData user_data);

/* Network */
EXC_API void exc_on_connection_status(ExcConnectionStatusCallback cb, ExcUserData user_data);
EXC_API void exc_on_connection_count(ExcConnectionCountCallback cb, ExcUserData user_data);

/* Mining */
EXC_API void exc_on_mining_status(ExcMiningStatusCallback cb, ExcUserData user_data);

/* Actor rename */
EXC_API void exc_on_actor_renamed(ExcActorRenamedCallback cb, ExcUserData user_data);

#ifdef __cplusplus
}
#endif

#endif /* EXTRACHAIN_C_H */
