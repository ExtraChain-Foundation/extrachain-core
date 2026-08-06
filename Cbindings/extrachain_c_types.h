/*
 * ExtraChain Core — C FFI Types
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EXTRACHAIN_C_TYPES_H
#define EXTRACHAIN_C_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Handle ─────────────────────────────────────────────────────── */

typedef uint64_t ExcHandle;
#define EXC_INVALID_HANDLE ((ExcHandle)0)

/* ── Error codes ────────────────────────────────────────────────── */

typedef enum ExcError {
    /* Generic 0-99 */
    EXC_OK = 0,
    EXC_ERR_UNKNOWN = 1,
    EXC_ERR_NOT_INITIALIZED = 2,
    EXC_ERR_ALREADY_INITIALIZED = 3,
    EXC_ERR_NULL_ARGUMENT = 4,
    EXC_ERR_INVALID_HANDLE = 5,
    EXC_ERR_INVALID_ARGUMENT = 6,
    EXC_ERR_NOT_LOGGED_IN = 7,
    EXC_ERR_DISPATCH_FAILED = 8,
    EXC_ERR_JSON_ERROR = 9,

    /* Account 100-199 */
    EXC_ERR_ACCOUNT_UNKNOWN = 100,
    EXC_ERR_ACCOUNT_EMPTY_HASH = 101,
    EXC_ERR_ACCOUNT_NO_PROFILES = 102,
    EXC_ERR_ACCOUNT_NO_AUTH = 103,
    EXC_ERR_ACCOUNT_MULTIPLE = 104,
    EXC_ERR_ACCOUNT_INVALID_PHRASE = 105,
    EXC_ERR_ACCOUNT_IMPORT_DATA_EMPTY = 106,
    EXC_ERR_ACCOUNT_IMPORT_CRED_EMPTY = 107,
    EXC_ERR_ACCOUNT_IMPORT_DECRYPT = 108,
    EXC_ERR_ACCOUNT_IMPORT_JSON = 109,
    EXC_ERR_ACCOUNT_IMPORT_FILE_NOT_FOUND = 110,
    EXC_ERR_ACCOUNT_IMPORT_FILE_READ = 111,
    EXC_ERR_ACCOUNT_IMPORT_FILE_EMPTY = 112,
    EXC_ERR_ACCOUNT_IMPORT_BASE64 = 113,
    EXC_ERR_ACCOUNT_IMPORT_ERROR = 114,
    EXC_ERR_ACCOUNT_EXPORT_NO_NETWORK = 115,
    EXC_ERR_ACCOUNT_EXPORT_EMPTY = 116,
    EXC_ERR_ACCOUNT_EXPORT_CRYPTO = 117,
    EXC_ERR_ACCOUNT_EXPORT_NO_ACTOR = 118,

    /* Transaction 200-299 */
    EXC_ERR_TX_UNKNOWN = 200,
    EXC_ERR_TX_NO_SENDER = 201,
    EXC_ERR_TX_EMPTY = 202,
    EXC_ERR_TX_NO_LAST_SECTION = 203,
    EXC_ERR_TX_INSUFFICIENT_FUNDS = 204,
    EXC_ERR_TX_NO_CURRENT_USER = 205,
    EXC_ERR_TX_ZERO_AMOUNT = 206,
    EXC_ERR_TX_PROVE_DUPLICATE = 207,
    EXC_ERR_TX_PROVE_WRONG_HASH = 208,
    EXC_ERR_TX_PROVE_IDENTICAL_SENDER_RECEIVER = 209,
    EXC_ERR_TX_PROVE_SENDER_ZERO = 210,
    EXC_ERR_TX_PROVE_RECEIVER_ZERO = 211,
    EXC_ERR_TX_PROVE_SENDER_NOT_EXISTS = 212,
    EXC_ERR_TX_PROVE_RECEIVER_NOT_EXISTS = 213,
    EXC_ERR_TX_PROVE_BALANCE_BELOW_ZERO = 214,
    EXC_ERR_TX_PROVE_MISSING_SIG = 215,
    EXC_ERR_TX_PROVE_INVALID_SIG = 216,

    /* Crypto 300-399 */
    EXC_ERR_CRYPTO_EMPTY_DATA = 300,
    EXC_ERR_CRYPTO_EMPTY_KEY = 301,
    EXC_ERR_CRYPTO_EMPTY_SIGN = 302,
    EXC_ERR_CRYPTO_ENCRYPT_FAILED = 303,
    EXC_ERR_CRYPTO_DECRYPT_FAILED = 304,
    EXC_ERR_CRYPTO_DATA_TOO_SHORT = 305,
    EXC_ERR_CRYPTO_DATA_TOO_LARGE = 306,
    EXC_ERR_CRYPTO_KEY_CONVERSION = 307,
    EXC_ERR_CRYPTO_AUTH_FAILED = 308,
    EXC_ERR_CRYPTO_INVALID_PATH = 309,
    EXC_ERR_CRYPTO_FILE_ACCESS = 310,
    EXC_ERR_CRYPTO_MNEMONIC_INVALID = 311,

    /* DFS 400-499 */
    EXC_ERR_DFS_UNKNOWN = 400,
    EXC_ERR_DFS_NOT_EXISTS = 401,
    EXC_ERR_DFS_NOT_FILE = 402,
    EXC_ERR_DFS_NOT_READABLE = 403,
    EXC_ERR_DFS_STORAGE_FULL = 404,
    EXC_ERR_DFS_ALREADY_EXISTS = 405,
    EXC_ERR_DFS_DIR_ERROR = 406,
    EXC_ERR_DFS_VECTOR_NOT_FOUND = 410,
    EXC_ERR_DFS_VECTOR_EMPTY = 411,
    EXC_ERR_DFS_VECTOR_ADD = 412,
    EXC_ERR_DFS_VECTOR_UPDATE = 413,
    EXC_ERR_DFS_VECTOR_DELETE = 414,
    EXC_ERR_DFS_COLLECTION_NOT_FOUND = 420,
    EXC_ERR_DFS_COLLECTION_EMPTY = 421,
    EXC_ERR_DFS_EXPORT_DIR_ROW_NOT_EXISTS = 430,
    EXC_ERR_DFS_EXPORT_NOT_READY = 431,
    EXC_ERR_DFS_EXPORT_BAD_PATH = 432,
    EXC_ERR_DFS_EXPORT_LOCAL_MISSING = 433,
    EXC_ERR_DFS_EXPORT_LOCAL_INVALID = 434,
    EXC_ERR_DFS_EXPORT_OUTPUT_DIR_MISSING = 435,
    EXC_ERR_DFS_EXPORT_NO_PERMISSIONS = 436,
    EXC_ERR_DFS_EXPORT_OUTPUT_EXISTS = 437,
    EXC_ERR_DFS_EXPORT_COPY_ERROR = 438,

    /* Chat 500-599 */
    EXC_ERR_CHAT_UNKNOWN = 500,

    /* Token 600-699 */
    EXC_ERR_TOKEN_NO_CONNECTIONS = 600,
    EXC_ERR_TOKEN_INVALID_AMOUNT = 601,
    EXC_ERR_TOKEN_INVALID_NAME = 602,
    EXC_ERR_TOKEN_EXISTS = 603,
    EXC_ERR_TOKEN_INVALID_TX = 604,
    EXC_ERR_TOKEN_INVALID_OWNER = 605,

    /* Import 700-799 */
    EXC_ERR_IMPORT_UNKNOWN = 700,

    /* DAG 800-899 */
    EXC_ERR_DAG_SECTION_NOT_FOUND = 800,
    EXC_ERR_DAG_TX_NOT_FOUND = 801,

    /* Contracts 900-999 */
    EXC_ERR_CONTRACT_NOT_FOUND        = 900,
    EXC_ERR_CONTRACT_INVALID_ARGUMENT = 901,
    EXC_ERR_CONTRACT_EXECUTION        = 902,
    EXC_ERR_CONTRACT_CONFLICT         = 903,
    EXC_ERR_CONTRACT_STORAGE          = 904,
    EXC_ERR_CONTRACT_UPGRADE_DENIED   = 905,
} ExcError;

/* ── Enums ──────────────────────────────────────────────────────── */

typedef enum ExcActorType {
    EXC_ACTOR_USER = 0,
    EXC_ACTOR_DAPP_MASTER = 1,
    EXC_ACTOR_SERVICE = 2,
} ExcActorType;

typedef enum ExcTransactionType {
    EXC_TX_GENESIS = 0,
    EXC_TX_REGULAR = 1,
    EXC_TX_INIT_CONTRACT = 2,
    EXC_TX_REPEATABLE = 3,
    EXC_TX_REWARD = 4,
    EXC_TX_BURN = 5,
    EXC_TX_CONVERSION = 6,
    EXC_TX_MINTING          = 7,
    EXC_TX_CONTRACT_DEPLOY  = 8,
    EXC_TX_CONTRACT_CALL    = 9,
    EXC_TX_CONTRACT_UPGRADE = 10,
    EXC_TX_BALANCE = 99,
    EXC_TX_UNKNOWN = 100,
} ExcTransactionType;

typedef enum ExcDagMode {
    EXC_DAG_MODE_FULL = 0,
    EXC_DAG_MODE_LIGHT = 1,
} ExcDagMode;

typedef enum ExcDagStatus {
    EXC_DAG_STATUS_STARTED = 0,
    EXC_DAG_STATUS_READY = 1,
    EXC_DAG_STATUS_FINAL = 2,
    EXC_DAG_STATUS_SYNC = 3,
    EXC_DAG_STATUS_MAYBE = 4,
    EXC_DAG_STATUS_TIMERED = 5,
} ExcDagStatus;

typedef enum ExcDfsMode {
    EXC_DFS_MODE_FULL = 0,
    EXC_DFS_MODE_LIGHT = 1,
} ExcDfsMode;

typedef enum ExcFileType {
    EXC_FILE_TYPE_FOLDER = 0,
    EXC_FILE_TYPE_FILE = 10,
    EXC_FILE_TYPE_COLLECTION = 20,
    EXC_FILE_TYPE_VECTOR = 30,
    EXC_FILE_TYPE_DICTIONARY = 40,
} ExcFileType;

typedef enum ExcFileState {
    EXC_FILE_STATE_REMOVED = 0,
    EXC_FILE_STATE_KNOWN = 1,
    EXC_FILE_STATE_READY = 2,
    EXC_FILE_STATE_PARTIAL = 3,
    EXC_FILE_STATE_PROCESSING = 4,
    EXC_FILE_STATE_UNKNOWN = 100,
} ExcFileState;

typedef enum ExcDataSecurity {
    EXC_DATA_SECURITY_PUBLIC = 0,
    EXC_DATA_SECURITY_ENCRYPTED = 1,
    EXC_DATA_SECURITY_SELF = 111,
    EXC_DATA_SECURITY_ACTOR = 222,
    EXC_DATA_SECURITY_KEY = 333,
} ExcDataSecurity;

typedef enum ExcServiceFolder {
    EXC_SERVICE_FOLDER_BASE = 0,
    EXC_SERVICE_FOLDER_COLLECTION = 1,
    EXC_SERVICE_FOLDER_COLLECTION_TEMPLATE = 2,
    EXC_SERVICE_FOLDER_CONTRACTS = 3,
    EXC_SERVICE_FOLDER_CHAT = 4,
} ExcServiceFolder;

typedef enum ExcChatType {
    EXC_CHAT_DIALOGUE = 0,
    EXC_CHAT_GROUP = 1,
    EXC_CHAT_CHANNEL = 2,
    EXC_CHAT_BOT = 3,
} ExcChatType;

typedef enum ExcChatMessageType {
    EXC_CHAT_MSG_TEXT = 0,
    EXC_CHAT_MSG_CREATED = 1,
    EXC_CHAT_MSG_INVITE = 2,
    EXC_CHAT_MSG_JOIN = 3,
    EXC_CHAT_MSG_IMAGE = 4,
    EXC_CHAT_MSG_GIF = 5,
    EXC_CHAT_MSG_AUDIO = 6,
    EXC_CHAT_MSG_VOICE = 7,
    EXC_CHAT_MSG_VIDEO = 8,
    EXC_CHAT_MSG_FILE = 9,
} ExcChatMessageType;

typedef enum ExcProfileType {
    EXC_PROFILE_OLD = 0,
    EXC_PROFILE_NEW = 1,
} ExcProfileType;

/* ── Byte buffer ────────────────────────────────────────────────── */

typedef struct ExcBytes {
    uint8_t* data;
    size_t   size;
} ExcBytes;

/* ── Mnemonic result ────────────────────────────────────────────── */

typedef struct ExcMnemonic {
    char*    phrase;      /* space-separated words, caller frees via exc_string_free */
    char*    main_id;     /* ActorId of main wallet */
    char*    profile_id;  /* ActorId of profile (system actor) */
} ExcMnemonic;

/* ── Balance entry ──────────────────────────────────────────────── */

typedef struct ExcBalanceEntry {
    char* actor_id;   /* caller frees via exc_string_free */
    char* token_id;   /* caller frees via exc_string_free */
    char* amount;     /* decimal string, caller frees via exc_string_free */
} ExcBalanceEntry;

typedef struct ExcBalanceList {
    ExcBalanceEntry* entries;
    size_t           count;
} ExcBalanceList;

/* ── String list ────────────────────────────────────────────────── */

typedef struct ExcStringList {
    char** items;
    size_t count;
} ExcStringList;

/* ── User data pointer for callbacks ────────────────────────────── */

typedef void* ExcUserData;

/* ── Callback typedefs ──────────────────────────────────────────── */

/* Node */
typedef void (*ExcNodeReadyCallback)(ExcUserData user_data);

/* DAG sync */
typedef void (*ExcDagSyncStartCallback)(const char* from_section, const char* to_section, ExcUserData user_data);
typedef void (*ExcDagSyncProgressCallback)(const char* section, ExcUserData user_data);
typedef void (*ExcDagSyncFinishCallback)(ExcUserData user_data);
typedef void (*ExcDagStatusCallback)(ExcDagStatus status, ExcUserData user_data);

/* Mining */
typedef void (*ExcMiningStatusCallback)(bool active, ExcUserData user_data);

/* DAG transactions */
typedef void (*ExcDagTxSendedCallback)(const char* section_id, const char* hash, ExcUserData user_data);
typedef void (*ExcDagTxApprovedCallback)(const char* section_id, const char* hash, ExcUserData user_data);
typedef void (*ExcDagTxNotApprovedCallback)(const char* section_id, const char* hash, ExcUserData user_data);
typedef void (*ExcSelfTxCallback)(ExcHandle tx_handle, int status_trx, ExcUserData user_data);

/* Chat */
typedef void (*ExcChatMessageCallback)(const char* owner_id, const char* file_id,
                                       const char* message_json, ExcUserData user_data);
typedef void (*ExcChatsLoadedCallback)(ExcUserData user_data);
typedef void (*ExcChatAddedCallback)(const char* chat_json, ExcUserData user_data);

/* DFS */
typedef void (*ExcDfsStoredCallback)(const char* owner_id, const char* dir_row_json, ExcUserData user_data);
typedef void (*ExcDfsDownloadedCallback)(const char* owner_id, const char* dir_row_json, ExcUserData user_data);
typedef void (*ExcDfsDownloadProgressCallback)(const char* owner_id, const char* file_id,
                                               int progress, ExcUserData user_data);

/* Network */
typedef void (*ExcConnectionStatusCallback)(bool connected, ExcUserData user_data);
typedef void (*ExcConnectionCountCallback)(int count, ExcUserData user_data);

/* Actor rename */
typedef void (*ExcActorRenamedCallback)(const char* actor_id, const char* name, ExcUserData user_data);

#ifdef __cplusplus
}
#endif

#endif /* EXTRACHAIN_C_TYPES_H */
