/*
 * ExtraChain Core — C FFI Usage Example
 *
 * Build:
 *   cmake -B build -S . -DEXTRACHAIN_BUILD_CBINDINGS=ON -DEXTRACHAIN_BUILD_CBINDINGS_EXAMPLE=ON
 *   cmake --build build
 *
 * Run:
 *   ./build/Cbindings/extrachain_c_example
 */

#include "extrachain_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Callback examples ──────────────────────────────────────────── */

static void on_node_ready(void* user_data) {
    (void)user_data;
    printf("[callback] Node is ready!\n");
}

static void on_dag_status(ExcDagStatus status, void* user_data) {
    (void)user_data;
    const char* names[] = { "Started", "Ready", "Final", "Sync", "Maybe", "Timered" };
    printf("[callback] DAG status: %s\n", names[status]);
}

static void on_connection_count(int count, void* user_data) {
    (void)user_data;
    printf("[callback] Active connections: %d\n", count);
}

static void on_self_tx(ExcHandle tx_handle, int status_trx, void* user_data) {
    (void)user_data;
    char* hash = NULL;
    char* amount = NULL;
    if (exc_transaction_get_hash(tx_handle, &hash) == EXC_OK &&
        exc_transaction_get_amount(tx_handle, &amount) == EXC_OK) {
        printf("[callback] Self transaction: hash=%s amount=%s status=%d\n",
               hash, amount, status_trx);
    }
    exc_string_free(hash);
    exc_string_free(amount);
    exc_transaction_free(tx_handle);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char* argv[]) {
    ExcError err;

    printf("ExtraChain C FFI Example\n");
    printf("Library version: %s\n", exc_version());
    printf("C API version: %u\n\n", (unsigned)exc_api_version());

    /* Register callbacks before init */
    exc_on_node_ready(on_node_ready, NULL);
    exc_on_dag_status(on_dag_status, NULL);
    exc_on_connection_count(on_connection_count, NULL);
    exc_on_self_tx(on_self_tx, NULL);

    /* Initialize node (background thread mode) */
    err = exc_init(argc, argv, 0);
    if (err != EXC_OK) {
        printf("Failed to initialize: %d\n", err);
        return 1;
    }
    printf("Node initialized.\n");

    /* Create a new profile */
    ExcMnemonic mnemonic = { 0 };
    err = exc_profile_create("demo_user", "demo_password", &mnemonic);
    if (err != EXC_OK) {
        printf("Failed to create profile: %d\n", err);
        exc_shutdown();
        return 1;
    }
    printf("Profile created!\n");
    printf("  Seed phrase: %s\n", mnemonic.phrase);
    printf("  Main wallet: %s\n", mnemonic.main_id);
    printf("  Profile ID:  %s\n", mnemonic.profile_id);

    /* Login */
    err = exc_login("demo_user", "demo_password");
    if (err != EXC_OK) {
        printf("Login failed: %d\n", err);
        exc_mnemonic_free(&mnemonic);
        exc_shutdown();
        return 1;
    }
    printf("Logged in.\n");

    /* Get current wallet */
    char* wallet_id = NULL;
    err = exc_wallet_current_id(&wallet_id);
    if (err == EXC_OK) {
        printf("Current wallet: %s\n", wallet_id);
    }

    /* Query balance */
    char* balance = NULL;
    err = exc_balance_query(wallet_id, NULL, &balance);
    if (err == EXC_OK) {
        printf("Balance: %s\n", balance);
    }
    exc_string_free(balance);

    /* Get DAG info */
    char* section = NULL;
    ExcDagStatus dag_status;
    ExcDagMode dag_mode;

    exc_dag_current_section(&section);
    exc_dag_status(&dag_status);
    exc_dag_mode(&dag_mode);

    printf("DAG: section=%s status=%d mode=%d\n",
           section ? section : "(null)", dag_status, dag_mode);
    exc_string_free(section);

    /* List wallets */
    ExcStringList wallet_list = { 0 };
    err = exc_wallet_list(&wallet_list);
    if (err == EXC_OK) {
        printf("Wallets (%zu):\n", wallet_list.count);
        for (size_t i = 0; i < wallet_list.count; ++i) {
            printf("  [%zu] %s\n", i, wallet_list.items[i]);
        }
    }
    exc_string_list_free(&wallet_list);

    /* DFS stats */
    uint64_t taken = 0, limit = 0, available = 0;
    exc_dfs_size_taken(&taken);
    exc_dfs_size_limit(&limit);
    exc_dfs_size_available(&available);
    printf("DFS: taken=%llu limit=%llu available=%llu\n",
           (unsigned long long)taken,
           (unsigned long long)limit,
           (unsigned long long)available);

    /* Network info */
    bool connected = false;
    int conn_count = 0;
    exc_network_is_connected(&connected);
    exc_network_connection_count(&conn_count);
    printf("Network: connected=%d connections=%d\n", connected, conn_count);

    /* Crypto (standalone, doesn't need login) */
    char* actor_id = NULL;
    char* secret_key = NULL;
    char* public_key = NULL;
    err = exc_crypto_generate_keypair(&actor_id, &secret_key, &public_key);
    if (err == EXC_OK) {
        printf("\nGenerated keypair:\n");
        printf("  Actor ID:   %s\n", actor_id);
        printf("  Public key: %.16s...\n", public_key);

        /* Sign and verify */
        const char* message = "Hello, ExtraChain!";
        char* signature = NULL;
        err = exc_crypto_sign((const uint8_t*)message, strlen(message),
                              secret_key, public_key, &signature);
        if (err == EXC_OK) {
            printf("  Signature:  %.16s...\n", signature);

            bool valid = false;
            exc_crypto_verify((const uint8_t*)message, strlen(message),
                              signature, public_key, &valid);
            printf("  Verify:     %s\n", valid ? "OK" : "FAILED");
        }
        exc_string_free(signature);
    }
    exc_string_free(actor_id);
    exc_string_free(secret_key);
    exc_string_free(public_key);

    /* Cleanup */
    exc_string_free(wallet_id);
    exc_mnemonic_free(&mnemonic);

    printf("\nLogout and shutdown...\n");
    exc_logout();
    exc_shutdown();

    printf("Done.\n");
    return 0;
}
