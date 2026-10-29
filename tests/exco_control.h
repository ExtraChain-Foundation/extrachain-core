#pragma once

#include <future>
#include <boost/asio/post.hpp>
#include <boost/json.hpp>

#include "contracts/contract_codec.h"
#include "contracts/contract_manager.h"
#include "contracts/toolchain_registry.h"
#include "consensus/consensus_service.h"

namespace ExtraChain::Consensus {
    class ConsensusStateTestFixture {
    public:
        static boost::json::object diagnostics(ConsensusService& service) {
            std::lock_guard     lock(service.mutex_);
            boost::json::object result;
            if (!service.consensus_)
                return result;
            const auto& state   = service.consensus_->engine().safety_state();
            result["finalized"] = state.finalized_height;
            result["applied"] =
                service.applied_checkpoint_.has_value() ? service.applied_checkpoint_.value().height : 0;
            result["voting"]            = service.voting_enabled_;
            result["paused"]            = service.voting_paused_;
            result["ancestor_requests"] = service.ancestor_requests_.size();
            result["rejected_batches"]  = service.rejected_batches_.size();
            std::string missing;
            const auto  frontier           = service.local_nonce_frontier(&missing);
            result["nonce_frontier_ready"] = frontier.has_value();
            if (!frontier.has_value())
                result["nonce_frontier_error"] = std::to_underlying(frontier.error());
            result["missing_ancestor"] = missing;
            boost::json::array pending;
            for (const auto& [hash, proposal] : service.pending_proposals_) {
                if (pending.size() == 32)
                    break;
                pending.emplace_back(boost::json::object {
                    { "hash", hash },
                    { "height", proposal.header.height },
                    { "has_batch", service.consensus_->engine().has_batch(hash) },
                    { "rejected", service.rejected_batches_.contains(hash) },
                });
            }
            result["pending"] = std::move(pending);
            return result;
        }
    };
} // namespace ExtraChain::Consensus

namespace ExcoStand {
    inline boost::json::object execute(ExtraChain::Core::ExtraChainNode& node,
                                       const boost::json::object&        request) {
        using namespace ExtraChain::Contracts;
        const auto text = [&](std::string_view key) {
            const auto* value = request.if_contains(key);
            return value && value->is_string() ? std::string(value->as_string()) : std::string {};
        };
        const auto          operation = text("operation");
        boost::json::object result { { "ok", false }, { "sequence", request.at("sequence") } };
        const auto          fail = [&](std::string_view detail) {
            result["error"] = detail;
            return result;
        };
        if (operation == "status") {
            result["consensus"] = ExtraChain::Consensus::ConsensusStateTestFixture::diagnostics(*node.consensus());
            result["network"]     = node.network_id().to_string();
            result["wallet"]      = node.account_controller()->current_wallet().id().to_string();
            result["section"]     = node.dag()->current_section().to_string();
            result["connections"] = node.network()->active_connections_count();
            result["finalized"]   = node.consensus()->metrics().finalized;
            result["downloads"]   = node.dfs()->download_manager().active_downloads_size();
            result["enabled"]     = node_enabled.load();
            result["projection_ready"] = node.dag()->state_projection_ready();
            result["projection_reason"] = node.dag()->state_projection().reason;
            boost::json::array peers;
            {
                auto connections = *node.network()->connections();
                for (const auto& socket : *connections) {
                    if (!socket)
                        continue;
                    peers.emplace_back(boost::json::object { { "identifier", socket->identifier() },
                                                             { "active", socket->is_active() },
                                                             { "closed", socket->is_closed() },
                                                             { "pending_bytes", socket->pending_bytes() },
                                                             { "queued_messages", socket->queue_size() },
                                                             { "bytes_incoming", socket->bytes_incoming() },
                                                             { "bytes_outgoing", socket->bytes_outgoing() } });
                }
            }
            result["peers"] = std::move(peers);
        } else if (operation == "inspect") {
            const auto record = node.contract_manager()->inspect(text("contract"));
            if (!record.has_value())
                return fail(record.error().detail);
            const auto& value             = record.value();
            const auto& version           = value.versions.at(value.active_version - 1);
            const auto& revision          = version.revisions.back();
            result["contract"]            = value.contract_id;
            result["owner"]               = value.owner_id;
            result["language"]            = value.language;
            result["version"]             = value.active_version;
            result["module_hash"]         = version.module_hash;
            result["state_hash"]          = revision.state_hash;
            result["revision"]            = revision.revision;
            result["section"]             = revision.block;
            result["transaction_hash"]    = revision.transaction_hash;
            result["checkpoint_revision"] = revision.checkpoint_revision;
            result["state"]               = Utils::to_base64(revision.state);
        } else if (operation == "query") {
            const auto id        = ActorId::create(text("contract"));
            const auto arguments = Codec::encode_json(text("arguments"));
            if (!id.has_value() || !arguments.has_value())
                return fail("Invalid query arguments");
            const auto output = node.query_contract(id.value(), text("method"), arguments.value());
            if (!output.has_value())
                return fail(output.error().detail);
            const auto decoded = Codec::decode_json(output.value().data);
            if (!decoded.has_value())
                return fail(decoded.error().detail);
            result["value"] = boost::json::parse(decoded.value());
        } else if (operation == "package") {
            const auto package = Json::deserialize<ToolchainPackage>(text("package"));
            if (!package.has_value())
                return fail("Invalid package record");
            const auto bytes = node.toolchain_registry()->package(package.value());
            if (!bytes.has_value())
                return fail(bytes.error().detail);
            result["size"] = bytes.value().size();
            result["hash"] = Utils::calculate_hash(std::string(reinterpret_cast<const char*>(bytes.value().data()),
                                                               bytes.value().size()),
                                                   Utils::HashAlgorithm::Blake3);
        } else if (operation == "upgrade") {
            const auto id        = ActorId::create(text("contract"));
            const auto module    = FileIo::read_all(text("path"));
            const auto arguments = Codec::encode_json(text("arguments"));
            if (!id.has_value() || !module.has_value() || !arguments.has_value())
                return fail("Invalid upgrade input");
            const auto& bytes = module.value();
            const auto  submitted =
                node.submit_contract_upgrade(id.value(),
                                             std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                       bytes.size()),
                                             arguments.value());
            if (!submitted.has_value())
                return fail(submitted.error().detail);
            result["transaction_hash"] = submitted.value().hash();
        } else if (operation == "publish-package" || operation == "publish-package-current") {
            const auto language = toolchain_language(text("language"));
            const auto content  = FileIo::read_all(text("path"));
            if (!language.has_value() || !content.has_value())
                return fail("Invalid package input");
            auto*      accounts = node.account_controller();
            auto&      profile  = accounts->profile(accounts->current_profile().system().id());
            const auto previous = accounts->current_wallet().id();
            if (operation != "publish-package-current" && !profile.change_current(node.network_id()))
                return fail("Canonical wallet is unavailable");
            const auto& bytes = content.value();
            const auto  package =
                node.toolchain_registry()->publish_package(language.value(),
                                                           "linux",
                                                           "x86_64",
                                                           "tar.xz",
                                                           text("version"),
                                                           std::span(reinterpret_cast<const std::uint8_t*>(
                                                                         bytes.data()),
                                                                     bytes.size()));
            profile.change_current(previous);
            if (!package.has_value())
                return fail(package.error().detail);
            result["package"] = boost::json::parse(Json::serialize(package.value()));
        } else if (operation == "publish-manifest") {
            const auto manifest = Json::deserialize<ToolchainManifest>(text("manifest"));
            if (!manifest.has_value())
                return fail("Invalid manifest input");
            auto*      accounts = node.account_controller();
            auto&      profile  = accounts->profile(accounts->current_profile().system().id());
            const auto previous = accounts->current_wallet().id();
            if (!profile.change_current(node.network_id()))
                return fail("Canonical wallet is unavailable");
            const auto published = node.toolchain_registry()->publish_manifest(manifest.value());
            profile.change_current(previous);
            if (!published.has_value())
                return fail(published.error().detail);
        } else if (operation == "manifest") {
            const auto language = toolchain_language(text("language"));
            if (!language.has_value())
                return fail(language.error());
            const auto manifest = node.toolchain_registry()->manifest(language.value());
            if (!manifest.has_value())
                return fail(manifest.error().detail);
            result["manifest"] = boost::json::parse(Json::serialize(manifest.value()));
        } else {
            return fail("Unknown ExCo stand operation");
        }
        result["ok"] = true;
        return result;
    }

    inline void poll(ExtraChain::Core::ExtraChainNode& node, std::size_t index) {
        const auto* root = std::getenv("EXCO_CONTROL");
        if (root == nullptr)
            return;
        const auto directory = std::filesystem::path(root) / std::to_string(index);
        const auto path      = directory / "request.json";
        if (!std::filesystem::exists(path))
            return;
        const auto content = FileIo::read_all(path);
        std::filesystem::remove(path);
        if (!content.has_value())
            return;
        auto promise = std::make_shared<std::promise<boost::json::object>>();
        auto future  = promise->get_future();
        boost::asio::post(node.serial_executor(), [&node, promise, content = content.value()] {
            try {
                promise->set_value(execute(node, boost::json::parse(content).as_object()));
            } catch (const std::exception& error) {
                promise->set_value({ { "ok", false }, { "error", error.what() } });
            }
        });
        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            throw std::runtime_error("ExCo stand command timed out");
        }
        const auto result =
            FileIo::write_atomic(directory / "response.json", boost::json::serialize(future.get()));
        if (!result.has_value())
            throw std::runtime_error("Cannot save ExCo stand response");
    }
} // namespace ExcoStand
