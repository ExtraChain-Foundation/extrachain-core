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

#include "dfs/dirs_manager.h"
#include "runtime/work_budget.h"
#include "runtime/runtime.h"
#include <boost/asio/post.hpp>

#include <algorithm>
#include <map>
#include <set>

#include "utils/hash.h"

#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "dfs/dfs_service.h"
#include "dfs/load_manager.h"
#include "dfs/vector_index.h"
#include "utils/exc_logs.h"
#include "chain/actor_index.h"

namespace {
    std::pair<std::string, std::uint64_t> local_vector_content(const ActorId &owner, const std::string &file_id) {
        const auto path = Dfs::Path::file_path(owner, file_id);
        if (!path.has_value())
            return { { }, 0 };
        DbConnector database(path.value());
        if (!database.open(false) || !database.table_exists("Vector"))
            return { { }, 0 };
        const auto columns = database.table_columns("Vector");
        if (columns.empty())
            return { { }, 0 };
        Dfs::VectorIndex index(database, columns.front().name);
        const auto       root = index.root();
        return root.has_value() ? std::pair { root.value().hash, root.value().tree.bytes }
                                : std::pair<std::string, std::uint64_t> { { }, 0 };
    }

    void use_local_vector_content(const ActorId &owner, Dfs::DirRow &row) {
        if (row.state == Dfs::FileState::Removed
            || (row.type != Dfs::FileType::Vector && row.type != Dfs::FileType::Dictionary))
            return;
        const auto [hash, bytes] = local_vector_content(owner, row.file_id);
        if (hash.empty()) {
            row.state = Dfs::FileState::Known;
            return;
        }
        row.hash  = hash;
        row.size  = bytes;
        row.state = Dfs::FileState::Ready;
    }
} // namespace

struct DirsManager::CatalogWork {
    enum class Kind {
        Rows,
        Digest
    };
    struct Pending {
        Kind                                  kind;
        std::string                           peer;
        Dfs::CatalogRowsRequest               request;
        std::chrono::steady_clock::time_point expires;
        bool                                  in_flight = false;
    };
    explicit CatalogWork(boost::asio::any_io_executor executor)
        : strand(boost::asio::make_strand(executor)) {
    }
    boost::asio::strand<boost::asio::any_io_executor> strand;
    ExtraChain::Core::WorkBudget                      budget { { 16 * 1024 * 1024, 32, 8 * 1024 * 1024, 8 } };
    std::mutex                                        mutex;
    std::map<std::string, Pending>                    pending;
    std::map<std::string, std::size_t>                scope_cursor;
    std::atomic_bool                                  stopped { false };
    bool track(const std::string &id, Kind kind, const std::string &peer, const Dfs::CatalogRowsRequest &request) {
        std::lock_guard lock(mutex);
        const auto      now = std::chrono::steady_clock::now();
        std::erase_if(pending, [&](const auto &entry) {
            return !entry.second.in_flight && entry.second.expires <= now;
        });
        if (stopped.load() || pending.size() >= 64)
            return false;
        std::size_t same_peer = 0;
        for (const auto &[key, item] : pending) {
            if (item.peer != peer)
                continue;
            ++same_peer;
            if (item.kind == kind && item.request.owners == request.owners)
                return false;
        }
        if (same_peer >= 8)
            return false;
        return pending.emplace(id, Pending { kind, peer, request, now + std::chrono::seconds(30) }).second;
    }
    std::optional<Pending> begin(const std::string &id, Kind kind, const std::string &peer) {
        std::lock_guard lock(mutex);
        const auto      found = pending.find(id);
        if (stopped.load() || found == pending.end() || found->second.kind != kind || found->second.peer != peer
            || found->second.in_flight || found->second.expires <= std::chrono::steady_clock::now())
            return std::nullopt;
        found->second.in_flight = true;
        return found->second;
    }
    void finish(const std::string &id) {
        std::lock_guard lock(mutex);
        pending.erase(id);
    }
    struct Finish {
        CatalogWork &state;
        std::string  id;
        ~Finish() {
            state.finish(id);
        }
    };
};

DirsManager::DirsManager(ExtraChain::Core::ExtraChainNode *node)
    : work_(std::make_shared<CatalogWork>(node->storage_executor()))
    , node(node) {
    // create dfs folder
    std::filesystem::create_directories(DfsB::DFS_FOLDER);

    // basic creation of dirs file
    auto db_res = Dfs::Tables::DirsFile::DirsSpace::create_file();
    if (!db_res.has_value()) {
        eFatal("[DirsManager] Can't create basic .dirs file");
    }
    db_ = db_res.value();

    // OLD DFS -> NEW DFS converter
    old_dfs_to_new_dfs_converter();
}

DirsManager::~DirsManager() {
    stop();
    db_->close();
}

void DirsManager::stop() {
    work_->budget.stop();
    std::lock_guard lock(work_->mutex);
    work_->stopped.store(true);
    work_->pending.clear();
}

void DirsManager::old_dfs_to_new_dfs_converter() {
    auto copy_data = [&](const std::string &dir_file, const std::string &owner_id) -> bool {
        std::unique_ptr<DbConnector> db_old = std::make_unique<DbConnector>(dir_file);
        if (!db_old->open()) {
            eCritical("DirsManager::old_dfs_to_new_dfs_converter, Can't open .dir file");
            return false;
        }

        static const std::string select_old_query =
            "SELECT file_id, prev_file_id, actor_id, hash, folder, name, size, "
            "created, last_modified, type, encryption, state, sign FROM Files;";

        auto old_db_data = db_old->select(select_old_query);
        db_old->close();

        db_->query("BEGIN TRANSACTION");
        for (auto &db_row : old_db_data) {
            db_row.emplace("owner_id", owner_id);
            if (auto it = db_row.find("prev_file_id"); it != db_row.end() && it->second.empty()) {
                db_row.erase(it);
            }
            db_->insert(Dfs::Tables::DirsFile::TableNameActorsFiles, db_row);
        }
        db_->query("COMMIT");
        return true;
    };
    try {
        static std::filesystem::path tempFileIsConverted = Dfs::Basic::DFS_FOLDER + "/.converted";
        if (std::filesystem::exists(tempFileIsConverted)) {
            eLog("DirsManager::old_dfs_to_new_dfs_converter, .converted file already exists.");
            return;
        }

        if (!std::filesystem::exists(Dfs::Basic::DFS_FOLDER)
            || !std::filesystem::is_directory(Dfs::Basic::DFS_FOLDER)) {
            eCritical("DirsManager::old_dfs_to_new_dfs_converter, directory {} not exist.",
                      Dfs::Basic::DFS_FOLDER);
            return;
        }

        int    processed_files = 0;
        int    deleted_files   = 0;
        size_t total           = std::distance(std::filesystem::directory_iterator(Dfs::Basic::DFS_FOLDER),
                                               std::filesystem::directory_iterator { });
        eLog("Total entries: {}", total);

        for (const auto &entry : std::filesystem::directory_iterator(Dfs::Basic::DFS_FOLDER)) {
            if (entry.is_directory()) {
                std::string sub_dir      = entry.path().string();
                std::string sub_dir_name = entry.path().filename().string();
                std::string dir_file     = sub_dir + "/.dir";

                if (std::filesystem::exists(dir_file)) {
                    processed_files++;

                    if (copy_data(dir_file, sub_dir_name)) {
                        try {
                            std::filesystem::remove(dir_file);
                            eLog("DirsManager::old_dfs_to_new_dfs_converter, file deleted {}.", dir_file);
                            deleted_files++;
                        } catch (const std::filesystem::filesystem_error &e) {
                            eCritical("DirsManager::old_dfs_to_new_dfs_converter, file deletion '{}' error: {}",
                                      dir_file,
                                      e.what());
                        }
                    } else {
                        eCritical(
                            "DirsManager::old_dfs_to_new_dfs_converter, copy data error. File is not deleted: {}",
                            dir_file);
                    }
                }

                if (std::filesystem::is_empty(sub_dir)) {
                    std::filesystem::remove(sub_dir);
                    eLog("DirsManager::old_dfs_to_new_dfs_converter, empty folder deleted {}.", sub_dir);
                }
            }
        }

        std::ofstream tempFile(tempFileIsConverted);
        if (tempFile) {
            tempFile << "DFS converted\n";
            tempFile.close();
            eLog("DirsManager::old_dfs_to_new_dfs_converter, .converted file created.");
        } else
            eLog("DirsManager::old_dfs_to_new_dfs_converter, .converted file cannot be created.");

        eLog("DirsManager::old_dfs_to_new_dfs_converter, work done. Proccessed files: {}, Deleted files: {}.",
             processed_files,
             deleted_files);

    } catch (const std::filesystem::filesystem_error &e) {
        eCritical("DirsManager::old_dfs_to_new_dfs_converter, filesystem error: {}", e.what());
    }
}

void DirsManager::update_dirs(const ActorId &actor_id, uint64_t last_modified) {
    Dfs::Tables::DirsFile::DirsSpace::update_row(db_, actor_id, last_modified);
}

std::shared_ptr<DbConnector> DirsManager::get_db_instance() {
    return db_;
}

// ---------------------------------------------------------------------------
// Content-based catalog sync (#75)
// ---------------------------------------------------------------------------

std::vector<Dfs::Packets::CatalogDigest> DirsManager::catalog_digests(const std::vector<ActorId> &only) {
    std::vector<Dfs::Packets::CatalogDigest> digests;
    const std::set<ActorId>                  filter(only.begin(), only.end());
    DbConnector                              reader(db_->file());
    if (!reader.open(false))
        return digests;
    auto rows = reader.select_while(
        "SELECT owner_id,file_id,sign,hash,type,state FROM ActorsFiles "
        "WHERE metadata_revision > 0 ORDER BY owner_id,file_id",
        "ActorsFiles");
    if (!rows)
        return digests;
    std::optional<ActorId> current;
    std::uint64_t          count = 0;
    blake3_hasher          hasher;
    blake3_hasher_init(&hasher);
    const auto flush = [&] {
        if (!current.has_value())
            return;
        std::array<std::uint8_t, BLAKE3_OUT_LEN> digest;
        blake3_hasher_finalize(&hasher, digest.data(), digest.size());
        digests.push_back({ .owner_id = current.value(),
                            .rows     = count,
                            .digest   = fmt::format("{:02x}", fmt::join(digest, "")) });
        blake3_hasher_init(&hasher);
        count = 0;
    };
    const auto append = [&](std::string_view value) {
        blake3_hasher_update(&hasher, value.data(), value.size());
    };
    while (rows->next()) {
        if (work_->stopped.load())
            return { };
        const auto owner = ActorId::create(rows->getString(0));
        if (!owner.has_value() || owner.value().is_zero() || (!filter.empty() && !filter.contains(owner.value())))
            continue;
        if (current.has_value() && current.value() != owner.value()) {
            flush();
            if (digests.size() > Dfs::CatalogOwnerLimit)
                return digests;
        }
        current            = owner.value();
        const auto file_id = rows->getString(1);
        auto       hash    = rows->getString(3);
        const auto type    = rows->getString(4);
        if (rows->getString(5) != std::to_string(std::to_underlying(Dfs::FileState::Removed))
            && (type == std::to_string(std::to_underlying(Dfs::FileType::Vector))
                || type == std::to_string(std::to_underlying(Dfs::FileType::Dictionary))))
            hash = local_vector_content(owner.value(), file_id).first;
        append(file_id);
        append(std::string_view("\0", 1));
        append(rows->getString(2));
        append(std::string_view("\0", 1));
        append(hash);
        append("\n");
        ++count;
    }
    if (rows->failed())
        return { };
    flush();
    return digests;
}

bool DirsManager::digest_answered(const std::string &identifier) {
    std::lock_guard lock(digest_mutex_);
    return digest_answered_.contains(identifier);
}

namespace {
    bool catalog_peer(const Responder &responder) {
        return responder.identifiers().size() == 1 && !responder.identifiers().begin()->empty()
               && responder.identifiers().begin()->size() <= 64;
    }
    bool valid_digests(const std::vector<Dfs::Packets::CatalogDigest> &digests) {
        if (digests.size() > Dfs::CatalogOwnerLimit)
            return false;
        std::set<ActorId> owners;
        for (const auto &digest : digests)
            if (digest.owner_id.is_zero() || !owners.insert(digest.owner_id).second || digest.digest.size() != 64
                || !std::ranges::all_of(digest.digest, [](char c) {
                       return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                   }))
                return false;
        return true;
    }
} // namespace

std::vector<ActorId> DirsManager::bounded_scope(const std::string &peer, const std::vector<ActorId> &owners) {
    auto ordered = owners;
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    std::erase_if(ordered, [](const auto &owner) {
        return owner.is_zero();
    });
    if (ordered.size() <= Dfs::CatalogOwnerLimit)
        return ordered;
    std::lock_guard lock(work_->mutex);
    if (work_->scope_cursor.size() >= 128 && !work_->scope_cursor.contains(peer))
        work_->scope_cursor.clear();
    auto &position = work_->scope_cursor[peer];
    position %= ordered.size();
    const auto           end = std::min(ordered.size(), position + Dfs::CatalogOwnerLimit);
    std::vector<ActorId> result(ordered.begin() + position, ordered.begin() + end);
    position = end == ordered.size() ? 0 : end;
    return result;
}

std::string DirsManager::request_catalog_rows(const Dfs::CatalogRowsRequest &request, const Responder &target) {
    if (!catalog_peer(target) || !Dfs::valid_catalog_request(request))
        return { };
    auto canonical = request;
    std::sort(canonical.owners.begin(), canonical.owners.end());
    const auto peer      = *target.identifiers().begin();
    const auto responder = target.with_new_message_id();
    if (!work_->track(responder.message_id(), CatalogWork::Kind::Rows, peer, canonical))
        return { };
    WireFormat::Scope scope(WireFormat::wire());
    if (responder.send_response(canonical, MessageType::DfsSyncDirRows, SendMode::Focused, MessageStatus::Request)
            .empty()) {
        work_->finish(responder.message_id());
        return { };
    }
    return responder.message_id();
}

std::string DirsManager::request_catalog_digest(const std::vector<ActorId> &allowed, const Responder &target) {
    if (!catalog_peer(target) || !Dfs::valid_catalog_request({ .owners = allowed }))
        return { };
    auto owners = allowed;
    std::sort(owners.begin(), owners.end());
    const auto peer      = *target.identifiers().begin();
    const auto responder = target.with_new_message_id();
    if (!work_->track(responder.message_id(), CatalogWork::Kind::Digest, peer, { .owners = owners }))
        return { };
    auto       digests  = catalog_digests(owners);
    const bool complete = digests.size() <= Dfs::CatalogOwnerLimit;
    if (!complete)
        digests.clear();
    const Dfs::Packets::CatalogDigestRequest request { .owners   = std::move(digests),
                                                       .allowed  = owners,
                                                       .complete = complete };
    WireFormat::Scope                        scope(WireFormat::wire());
    if (responder.send_response(request, MessageType::DfsSyncDigest, SendMode::Focused, MessageStatus::Request)
            .empty()) {
        work_->finish(responder.message_id());
        return { };
    }
    return responder.message_id();
}

void DirsManager::sync_digest(const std::string &identifier, const std::vector<ActorId> &allowed) {
    Responder target(node->network());
    target.add_identifier(identifier);
    request_catalog_digest(bounded_scope(identifier, allowed), target);
}

void DirsManager::temp_sync_all(const std::string &identifier) {
    Responder target(node->network());
    target.add_identifier(identifier);
    request_catalog_rows({ }, target);
}

void DirsManager::temp_sync_actors(const std::string &identifier, const std::vector<ActorId> &actors) {
    if (actors.empty())
        return;
    Responder target(node->network());
    target.add_identifier(identifier);
    request_catalog_rows({ .owners = bounded_scope(identifier, actors) }, target);
}

void DirsManager::network_request_catalog_rows(const Dfs::CatalogRowsRequest &request,
                                               const Responder               &responder) {
    if (!catalog_peer(responder) || !Dfs::valid_catalog_request(request))
        return;
    const auto ticket = work_->budget.reserve(*responder.identifiers().begin(), 256 * 1024);
    if (!ticket)
        return;
    auto work = [this, request, responder, ticket] {
        if (ticket->stopped())
            return;
        auto page = Dfs::read_catalog_page(db_, request);
        if (!page.has_value())
            return;
        for (auto &row : page.value().rows) {
            if (ticket->stopped())
                return;
            use_local_vector_content(row.owner_id, row);
        }
        WireFormat::Scope scope(WireFormat::wire());
        responder.send_response(page.value(),
                                MessageType::DfsSyncDirRows,
                                SendMode::Focused,
                                MessageStatus::Response);
    };
    boost::asio::post(work_->strand,
                      ExtraChain::Core::Runtime::guard_handler("catalog page request", std::move(work)));
}

void DirsManager::merge_catalog_rows(const std::vector<Dfs::DirRow> &rows, const Responder &responder) {
    std::map<ActorId, std::vector<Dfs::DirRow>> downloads;
    const bool                                  selective = node->dfs()->mode() == DfsMode::Selective;
    const auto              allowed = selective ? node->dfs()->startup_sync_actors() : std::vector<ActorId> { };
    const std::set<ActorId> filter(allowed.begin(), allowed.end());
    for (const auto &row : rows) {
        if (!node_enabled.load())
            return;
        if (selective && !filter.contains(row.owner_id))
            continue;
        const auto accepted = node->dfs()->accept_catalog_row(row.owner_id, row);
        if (!accepted.has_value())
            continue;
        auto       &todo   = downloads[row.owner_id];
        const auto &stored = accepted.value().current;
        if (stored.state == Dfs::FileState::Removed)
            continue;
        const bool vector = stored.type == Dfs::FileType::Vector || stored.type == Dfs::FileType::Dictionary;
        if (stored.type == Dfs::FileType::File || vector) {
            const auto &target = vector ? row : stored;
            if (!node->dfs()->is_file_already_downloaded(row.owner_id, stored.file_id, target.hash))
                todo.push_back(target);
        }
        if (accepted.value().changed && node->dfs()->mode() == DfsMode::Full)
            node->dfs()->broadcast_stored(row.owner_id, stored);
    }
    for (const auto &[owner, todo] : downloads) {
        Dfs::Tables::DirsFile::DirsSpace::update_from_files(db_, owner);
        node->dfs()->download_manager().add_to_queue(owner, todo, *responder.identifiers().begin());
    }
}

void DirsManager::network_response_dir_rows(std::string_view data, const Responder &responder) {
    if (!catalog_peer(responder) || data.size() > Dfs::CatalogPageBytes)
        return;
    const auto pending =
        work_->begin(responder.message_id(), CatalogWork::Kind::Rows, *responder.identifiers().begin());
    if (!pending.has_value())
        return;
    const auto ticket = work_->budget.reserve(*responder.identifiers().begin(), data.size());
    if (!ticket) {
        work_->finish(responder.message_id());
        return;
    }
    auto work = [this, state = work_, pending = pending.value(), responder, ticket, data = std::string(data)] {
        CatalogWork::Finish finish { *state, responder.message_id() };
        if (ticket->stopped() || !MessagePack::has_bounded_structure(data, 32768, 4096, 8))
            return;
        const auto page = MessagePack::deserialize<Dfs::CatalogRowsPage>(data);
        if (!page.has_value() || !Dfs::valid_catalog_page(page.value(), pending.request))
            return;
        merge_catalog_rows(page.value().rows, responder);
        if (ticket->stopped())
            return;
        state->finish(responder.message_id());
        node->dfs()->mark_startup_sync_response();
        if (page.value().next.has_value())
            request_catalog_rows({ .owners = pending.request.owners, .after = page.value().next.value() },
                                 responder);
    };
    boost::asio::post(work_->strand,
                      ExtraChain::Core::Runtime::guard_handler("catalog page response", std::move(work)));
}

void DirsManager::network_request_digest(const Dfs::Packets::CatalogDigestRequest &request,
                                         const Responder                          &responder) {
    if (!catalog_peer(responder) || !valid_digests(request.owners)
        || !Dfs::valid_catalog_request({ .owners = request.allowed })
        || (!request.complete && !request.owners.empty()))
        return;
    const std::set<ActorId> allowed(request.allowed.begin(), request.allowed.end());
    for (const auto &digest : request.owners)
        if (!allowed.empty() && !allowed.contains(digest.owner_id))
            return;
    const auto ticket = work_->budget.reserve(*responder.identifiers().begin(), 1024 * 1024);
    if (!ticket)
        return;
    auto work = [this, request, responder, ticket] {
        if (ticket->stopped())
            return;
        const auto                       local = catalog_digests(request.allowed);
        Dfs::Packets::CatalogDigestReply reply;
        std::vector<ActorId>             pull;
        const bool                       full_pull = !request.complete || local.size() > Dfs::CatalogOwnerLimit;
        if (local.size() > Dfs::CatalogOwnerLimit) {
            reply.full_catalog = true;
        } else {
            std::map<ActorId, std::string> remote;
            std::map<ActorId, std::string> own;
            for (const auto &digest : request.owners)
                remote.emplace(digest.owner_id, digest.digest);
            for (const auto &digest : local) {
                own.emplace(digest.owner_id, digest.digest);
                const auto found = remote.find(digest.owner_id);
                if (found == remote.end() || found->second != digest.digest)
                    reply.mismatched.push_back(digest);
            }
            for (const auto &digest : request.owners) {
                const auto found = own.find(digest.owner_id);
                if (found == own.end())
                    reply.unknown.push_back(digest.owner_id);
                if (found == own.end() || found->second != digest.digest)
                    pull.push_back(digest.owner_id);
            }
        }
        if (ticket->stopped())
            return;
        WireFormat::Scope scope(WireFormat::wire());
        responder.send_response(reply,
                                MessageType::DfsSyncDigestReply,
                                SendMode::Focused,
                                MessageStatus::Response);
        if (full_pull)
            request_catalog_rows({ .owners = request.allowed }, responder);
        else if (!pull.empty())
            request_catalog_rows({ .owners = std::move(pull) }, responder);
    };
    boost::asio::post(work_->strand,
                      ExtraChain::Core::Runtime::guard_handler("catalog digest request", std::move(work)));
}

void DirsManager::network_response_digest(std::string_view data, const Responder &responder) {
    if (!catalog_peer(responder) || data.size() > 1024 * 1024)
        return;
    const auto pending =
        work_->begin(responder.message_id(), CatalogWork::Kind::Digest, *responder.identifiers().begin());
    if (!pending.has_value())
        return;
    const auto ticket = work_->budget.reserve(*responder.identifiers().begin(), data.size());
    if (!ticket) {
        work_->finish(responder.message_id());
        return;
    }
    auto work = [this, state = work_, pending = pending.value(), responder, ticket, data = std::string(data)] {
        CatalogWork::Finish finish { *state, responder.message_id() };
        if (ticket->stopped() || !MessagePack::has_bounded_structure(data, 65536, 8192, 8))
            return;
        const auto decoded = MessagePack::deserialize<Dfs::Packets::CatalogDigestReply>(data);
        if (!decoded.has_value())
            return;
        const auto &reply = decoded.value();
        if (!valid_digests(reply.mismatched) || !Dfs::valid_catalog_request({ .owners = reply.unknown })
            || (reply.full_catalog && (!reply.mismatched.empty() || !reply.unknown.empty())))
            return;
        const std::set<ActorId> allowed(pending.request.owners.begin(), pending.request.owners.end());
        std::vector<ActorId>    owners;
        for (const auto &digest : reply.mismatched) {
            if (!allowed.empty() && !allowed.contains(digest.owner_id))
                return;
            owners.push_back(digest.owner_id);
        }
        {
            std::lock_guard lock(digest_mutex_);
            if (digest_answered_.size() >= 128)
                digest_answered_.clear();
            digest_answered_.insert(pending.peer);
        }
        node->dfs()->mark_startup_sync_response();
        if (reply.full_catalog)
            request_catalog_rows({ .owners = pending.request.owners }, responder);
        else if (!owners.empty())
            request_catalog_rows({ .owners = std::move(owners) }, responder);
    };
    boost::asio::post(work_->strand,
                      ExtraChain::Core::Runtime::guard_handler("catalog digest response", std::move(work)));
}
