/*
 * ExtraChain Core — C FFI Account Management
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_manager.h"
#include "chain/private_profile.h"
#include "encryption/encryption_tools.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

extern "C" {

EXC_API ExcError exc_profile_create(const char* login, const char* password,
                                    ExcMnemonic* out_mnemonic) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(login);
    EXC_CHECK_NULL(password);
    EXC_CHECK_NULL(out_mnemonic);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();

        std::string hash = Utils::calculate_hash(std::string(login) + std::string(password));

        SeedProfile seed_profile = ac->create_profile(hash, ActorType::User);

        /* Get mnemonic words */
        auto words = ac->seed_mnemonic();
        std::string phrase;
        for (size_t i = 0; i < words.size(); ++i) {
            if (i > 0) phrase += " ";
            phrase += words[i];
        }

        /* Get the main wallet ID */
        auto main_actor = ac->current_profile().main();
        std::string main_id;
        if (main_actor.has_value()) {
            main_id = main_actor.value().get().id().to_string();
        }

        /* Get profile (system) actor ID */
        std::string profile_id = ac->current_profile().system_id().to_string();

        out_mnemonic->phrase = exc_strdup(phrase);
        out_mnemonic->main_id = exc_strdup(main_id);
        out_mnemonic->profile_id = exc_strdup(profile_id);

        gs.node->network()->start_network();
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_import_seed(const char* login, const char* password,
                                         const char* phrase, char** out_main_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(login);
    EXC_CHECK_NULL(password);
    EXC_CHECK_NULL(phrase);
    EXC_CHECK_NULL(out_main_id);

    ExcError result = EXC_OK;
    *out_main_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();

        bool success = ac->import_seed_phrase(std::string(login), std::string(password), std::string(phrase));
        if (!success) {
            result = EXC_ERR_ACCOUNT_INVALID_PHRASE;
            return;
        }

        auto main_actor = ac->current_profile().main();
        if (main_actor.has_value()) {
            *out_main_id = exc_strdup(main_actor.value().get().id().to_string());
        }

        gs.node->network()->start_network();
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_import_data(const char* data, const char* login,
                                         const char* password, char** out_main_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(login);
    EXC_CHECK_NULL(password);
    EXC_CHECK_NULL(out_main_id);

    ExcError result = EXC_OK;
    *out_main_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto res = gs.node->import_profile(std::string(data), std::string(login), std::string(password));
        if (!res.has_value()) {
            switch (res.error()) {
            case ImportProfileError::DataEmpty:         result = EXC_ERR_ACCOUNT_IMPORT_DATA_EMPTY; break;
            case ImportProfileError::LoginPasswordEmpty: result = EXC_ERR_ACCOUNT_IMPORT_CRED_EMPTY; break;
            case ImportProfileError::DecryptError:      result = EXC_ERR_ACCOUNT_IMPORT_DECRYPT; break;
            case ImportProfileError::IncorrectJson:     result = EXC_ERR_ACCOUNT_IMPORT_JSON; break;
            default:                                    result = EXC_ERR_ACCOUNT_UNKNOWN; break;
            }
            return;
        }
        *out_main_id = exc_strdup(res.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_import_file(const char* file_path, const char* login,
                                         const char* password, char** out_main_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(file_path);
    EXC_CHECK_NULL(login);
    EXC_CHECK_NULL(password);
    EXC_CHECK_NULL(out_main_id);

    ExcError result = EXC_OK;
    *out_main_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto res = gs.node->import_profile_file(std::string(file_path), std::string(login), std::string(password));
        if (!res.has_value()) {
            switch (res.error()) {
            case ImportProfileFileError::LoginPasswordEmpty: result = EXC_ERR_ACCOUNT_IMPORT_CRED_EMPTY; break;
            case ImportProfileFileError::FileNotFound:       result = EXC_ERR_ACCOUNT_IMPORT_FILE_NOT_FOUND; break;
            case ImportProfileFileError::FileReadError:      result = EXC_ERR_ACCOUNT_IMPORT_FILE_READ; break;
            case ImportProfileFileError::FileEmpty:          result = EXC_ERR_ACCOUNT_IMPORT_FILE_EMPTY; break;
            case ImportProfileFileError::Base64DecodeError:  result = EXC_ERR_ACCOUNT_IMPORT_BASE64; break;
            case ImportProfileFileError::ImportError:        result = EXC_ERR_ACCOUNT_IMPORT_ERROR; break;
            default:                                         result = EXC_ERR_ACCOUNT_UNKNOWN; break;
            }
            return;
        }
        *out_main_id = exc_strdup(res.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_export(char** out_data) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_data);

    ExcError result = EXC_OK;
    *out_data = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto res = gs.node->export_profile();
        if (!res.has_value()) {
            switch (res.error()) {
            case ImportError::NoNetworkId:  result = EXC_ERR_ACCOUNT_EXPORT_NO_NETWORK; break;
            case ImportError::EmptyProfile: result = EXC_ERR_ACCOUNT_EXPORT_EMPTY; break;
            case ImportError::CryptoError:  result = EXC_ERR_ACCOUNT_EXPORT_CRYPTO; break;
            case ImportError::NoActor:      result = EXC_ERR_ACCOUNT_EXPORT_NO_ACTOR; break;
            default:                        result = EXC_ERR_ACCOUNT_UNKNOWN; break;
            }
            return;
        }
        *out_data = exc_strdup(res.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_wallet_create(char** out_wallet_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_wallet_id);

    ExcError result = EXC_OK;
    *out_wallet_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto actor = ac->create_wallet();
        if (actor.empty()) {
            result = EXC_ERR_ACCOUNT_UNKNOWN;
            return;
        }
        *out_wallet_id = exc_strdup(actor.id().to_string());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_wallet_current_id(char** out_wallet_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_wallet_id);

    ExcError result = EXC_OK;
    *out_wallet_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto& wallet = ac->current_wallet();
        if (wallet.empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }
        *out_wallet_id = exc_strdup(wallet.id().to_string());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_wallet_list(ExcStringList* out_list) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_list);

    ExcError result = EXC_OK;
    out_list->items = nullptr;
    out_list->count = 0;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto ids = ac->accounts_ids();

        if (ids.empty()) {
            out_list->items = nullptr;
            out_list->count = 0;
            return;
        }

        out_list->count = ids.size();
        out_list->items = static_cast<char**>(std::malloc(sizeof(char*) * ids.size()));
        if (!out_list->items) {
            out_list->count = 0;
            result = EXC_ERR_UNKNOWN;
            return;
        }
        for (size_t i = 0; i < ids.size(); ++i) {
            out_list->items[i] = exc_strdup(ids[i].to_string());
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_main_id(char** out_main_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_main_id);

    ExcError result = EXC_OK;
    *out_main_id = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto main_actor = ac->current_profile().main();
        if (!main_actor.has_value()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }
        *out_main_id = exc_strdup(main_actor.value().get().id().to_string());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API bool exc_mnemonic_validate(const char* phrase) {
    if (!phrase) return false;
    return Cryptography::validate_mnemonic(std::string(phrase));
}

EXC_API ExcError exc_profile_type(ExcProfileType* out_type) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_type);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto pt = ac->profile_type();
        *out_type = (pt == ProfileType::New) ? EXC_PROFILE_NEW : EXC_PROFILE_OLD;
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_profile_seed_phrase(char** out_phrase) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_phrase);

    ExcError result = EXC_OK;
    *out_phrase = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* ac = gs.node->account_controller();
        auto words = ac->seed_mnemonic();
        if (words.empty()) {
            result = EXC_ERR_ACCOUNT_UNKNOWN;
            return;
        }
        std::string phrase;
        for (size_t i = 0; i < words.size(); ++i) {
            if (i > 0) phrase += " ";
            phrase += words[i];
        }
        *out_phrase = exc_strdup(phrase);
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
