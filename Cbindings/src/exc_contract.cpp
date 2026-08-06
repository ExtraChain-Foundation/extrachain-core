/*
 * ExtraChain Core — C FFI WebAssembly Contracts
 */

#include "exc_internal.h"

#include <span>

#include <boost/describe.hpp>

#include "contracts/contract_manager.h"
#include "managers/extrachain_node.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

namespace {
    struct ContractSummary {
        std::string   contract_id;
        std::string   owner_id;
        std::string   kind;
        std::uint32_t version;
        std::uint64_t revision;
        std::string   module_hash;
        std::string   state_hash;
        std::string   transaction_hash;
    };
    BOOST_DESCRIBE_STRUCT(
        ContractSummary,
        (),
        (contract_id, owner_id, kind, version, revision, module_hash, state_hash, transaction_hash))

    ExcError contract_error(ExtraChain::Contracts::ContractError error) {
        using ExtraChain::Contracts::ContractError;
        switch (error) {
        case ContractError::NotFound:
            return EXC_ERR_CONTRACT_NOT_FOUND;
        case ContractError::InvalidArguments:
        case ContractError::InvalidOwner:
        case ContractError::InvalidModule:
        case ContractError::InvalidResponse:
            return EXC_ERR_CONTRACT_INVALID_ARGUMENT;
        case ContractError::Conflict:
            return EXC_ERR_CONTRACT_CONFLICT;
        case ContractError::StorageError:
        case ContractError::AlreadyExists:
            return EXC_ERR_CONTRACT_STORAGE;
        case ContractError::UpgradeDenied:
            return EXC_ERR_CONTRACT_UPGRADE_DENIED;
        case ContractError::ExecutionFailed:
        case ContractError::StateTooLarge:
        case ContractError::TooManyEvents:
            return EXC_ERR_CONTRACT_EXECUTION;
        }
        return EXC_ERR_UNKNOWN;
    }

    std::span<const std::uint8_t> bytes(const std::uint8_t* data, std::size_t size) {
        return size == 0 ? std::span<const std::uint8_t>() : std::span(data, size);
    }
} // namespace

extern "C" {

EXC_API ExcError exc_contract_deploy(const char*    kind,
                                     const uint8_t* module,
                                     size_t         module_len,
                                     const uint8_t* init_arguments,
                                     size_t         init_arguments_len,
                                     ExcHandle*     out_tx_handle) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(kind);
    EXC_CHECK_NULL(module);
    EXC_CHECK_NULL(out_tx_handle);
    *out_tx_handle = EXC_INVALID_HANDLE;
    if (module_len == 0 || (init_arguments_len > 0 && init_arguments == nullptr)) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    ExcError result = EXC_OK;
    bool     ok     = dispatch_sync([&]() {
        auto submitted =
            GlobalState::instance().node->submit_contract_deploy(kind,
                                                                 bytes(module, module_len),
                                                                 bytes(init_arguments, init_arguments_len));
        if (!submitted.has_value()) {
            result = contract_error(submitted.error().error);
            return;
        }
        *out_tx_handle = HandleTable::instance().store(std::move(*submitted));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_contract_call(const char*    contract_id,
                                   const char*    method,
                                   const uint8_t* arguments,
                                   size_t         arguments_len,
                                   ExcHandle*     out_tx_handle) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(contract_id);
    EXC_CHECK_NULL(method);
    EXC_CHECK_NULL(out_tx_handle);
    *out_tx_handle = EXC_INVALID_HANDLE;
    if (arguments_len > 0 && arguments == nullptr) {
        return EXC_ERR_NULL_ARGUMENT;
    }
    auto id = ActorId::create(contract_id);
    if (!id.has_value()) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    ExcError result = EXC_OK;
    bool     ok     = dispatch_sync([&]() {
        auto submitted =
            GlobalState::instance().node->submit_contract_call(*id, method, bytes(arguments, arguments_len));
        if (!submitted.has_value()) {
            result = contract_error(submitted.error().error);
            return;
        }
        *out_tx_handle = HandleTable::instance().store(std::move(*submitted));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_contract_query(const char*    contract_id,
                                    const char*    method,
                                    const uint8_t* arguments,
                                    size_t         arguments_len,
                                    char**         out_result_base64) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(contract_id);
    EXC_CHECK_NULL(method);
    EXC_CHECK_NULL(out_result_base64);
    if (arguments_len > 0 && arguments == nullptr) {
        return EXC_ERR_NULL_ARGUMENT;
    }
    auto id = ActorId::create(contract_id);
    if (!id.has_value()) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    *out_result_base64 = nullptr;
    ExcError result    = EXC_OK;
    bool     ok        = dispatch_sync([&]() {
        auto receipt = GlobalState::instance().node->query_contract(*id, method, bytes(arguments, arguments_len));
        if (!receipt.has_value()) {
            result = contract_error(receipt.error().error);
            return;
        }
        *out_result_base64 = exc_strdup(Utils::to_base64(receipt->data));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_contract_upgrade(const char*    contract_id,
                                      const uint8_t* module,
                                      size_t         module_len,
                                      const uint8_t* migration_arguments,
                                      size_t         migration_arguments_len,
                                      ExcHandle*     out_tx_handle) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(contract_id);
    EXC_CHECK_NULL(module);
    EXC_CHECK_NULL(out_tx_handle);
    *out_tx_handle = EXC_INVALID_HANDLE;
    if (module_len == 0 || (migration_arguments_len > 0 && migration_arguments == nullptr)) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    auto id = ActorId::create(contract_id);
    if (!id.has_value()) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    ExcError result = EXC_OK;
    bool     ok     = dispatch_sync([&]() {
        auto submitted = GlobalState::instance().node->submit_contract_upgrade(*id,
                                                                               bytes(module, module_len),
                                                                               bytes(migration_arguments,
                                                                                     migration_arguments_len));
        if (!submitted.has_value()) {
            result = contract_error(submitted.error().error);
            return;
        }
        *out_tx_handle = HandleTable::instance().store(std::move(*submitted));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_contract_inspect(const char* contract_id, char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(contract_id);
    EXC_CHECK_NULL(out_json);
    auto id = ActorId::create(contract_id);
    if (!id.has_value()) {
        return EXC_ERR_INVALID_ARGUMENT;
    }
    *out_json       = nullptr;
    ExcError result = EXC_OK;
    bool     ok     = dispatch_sync([&]() {
        auto record = GlobalState::instance().node->contract_manager()->inspect(id->to_string());
        if (!record.has_value()) {
            result = contract_error(record.error().error);
            return;
        }
        const auto& version  = record->versions.at(record->active_version - 1);
        const auto& revision = version.revisions.back();
        auto        summary  = ContractSummary {
                            .contract_id      = record->contract_id,
                            .owner_id         = record->owner_id,
                            .kind             = record->kind,
                            .version          = version.version,
                            .revision         = revision.revision,
                            .module_hash      = version.module_hash,
                            .state_hash       = revision.state_hash,
                            .transaction_hash = revision.transaction_hash,
        };
        *out_json = exc_strdup(Json::serialize(summary));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
