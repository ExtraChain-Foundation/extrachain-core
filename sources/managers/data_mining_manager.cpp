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

#include "managers/data_mining_manager.h"

#include <fstream>
#include <mutex>
#include <fmt/ranges.h>

#include "consensus/consensus_service.h"
#include "consensus/storage_index.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "utils/file_io.h"
#include "utils/msgpack_limits.h"

using namespace ExtraChain::Consensus;

namespace {
    constexpr std::size_t       MaximumLocalJobs              = MaximumMiningDatasets;
    constexpr std::size_t       MaximumLocalJobBytes          = 4 * 1024 * 1024;
    constexpr std::size_t       MaximumSubmissionsPerProgress = 8;
    constexpr auto              ProgressRetryInterval         = std::chrono::milliseconds(250);
    const std::filesystem::path LocalJobPath                  = "consensus/mining-local.msgpack";

    struct LocalJob {
        ActorId                       owner;
        std::string                   file_id;
        std::string                   hash;
        std::uint64_t                 bytes = 0;
        std::optional<StorageDataset> dataset;
        std::string                   pending;
        ActorId                       provider;
        IntentOperation               operation = IntentOperation::StorageRegister;
        std::uint64_t                 epoch     = 0;
        bool                          available = true;
        bool                          indexed   = false;
        MSGPACK_DEFINE(owner,
                       file_id,
                       hash,
                       bytes,
                       dataset,
                       pending,
                       provider,
                       operation,
                       epoch,
                       available,
                       indexed)
    };

    std::string job_key(const LocalJob& job) {
        return job.owner.to_string() + ":" + job.file_id + ":" + job.hash;
    }
    std::filesystem::path index_path(const LocalJob& job) {
        return std::filesystem::path("consensus/mining-index")
               / (Utils::calculate_hash(job_key(job) + ":" + job.hash) + ".idx");
    }

    MerkleValueReader file_reader(const std::filesystem::path& path, std::uint64_t bytes) {
        auto input = std::make_shared<std::ifstream>(path, std::ios::binary);
        return [input, bytes](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
            if (index > UINT64_MAX / StorageChunkBytes)
                return std::unexpected(ConsensusError::InvalidProof);
            const auto offset = index * StorageChunkBytes;
            if (offset >= bytes || offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
                return std::unexpected(ConsensusError::InvalidProof);
            const auto count =
                static_cast<std::size_t>(std::min<std::uint64_t>(StorageChunkBytes, bytes - offset));
            input->clear();
            input->seekg(static_cast<std::streamoff>(offset));
            std::string chunk(count, '\0');
            if (!input->read(chunk.data(), static_cast<std::streamsize>(count)))
                return std::unexpected(ConsensusError::StorageFailure);
            return chunk;
        };
    }
} // namespace

struct DataMiningManager::Work : std::enable_shared_from_this<Work> {
    ExtraChain::Core::ExtraChainNode*                            node;
    std::mutex                                                   mutex;
    std::mutex                                                   progress_mutex;
    // Consensus progress arrives with every consensus message, relayed duplicates and
    // timeout certificates included. The mining work state changes only with a new
    // certificate or finalized checkpoint, while every pass copies the whole
    // MiningState once and again for each submission. Guarded by progress_mutex.
    std::uint64_t                                                progress_certificates = UINT64_MAX;
    std::uint64_t                                                progress_finalized    = UINT64_MAX;
    std::chrono::steady_clock::time_point                        progress_attempt { };
    std::map<std::string, LocalJob>                              jobs;
    std::string                                                  last_submitted_job;
    std::map<std::string, std::chrono::steady_clock::time_point> retry_after;
    std::vector<boost::signals2::scoped_connection>              connections;
    bool                                                         enabled   = false;
    bool                                                         stopped   = false;
    bool                                                         preparing = false;

    explicit Work(ExtraChain::Core::ExtraChainNode* value)
        : node(value) {
    }

    void save() {
        std::vector<LocalJob> values;
        {
            std::lock_guard lock(mutex);
            if (stopped)
                return;
            for (const auto& [_, job] : jobs)
                if (job.dataset.has_value())
                    values.push_back(job);
        }
        const auto bytes = MessagePack::serialize(values);
        if (bytes.size() > MaximumLocalJobBytes || !FileIo::write_private_atomic(LocalJobPath, bytes).has_value())
            eWarning("[Mining] Local storage jobs could not be saved");
    }

    void restore() {
        std::ifstream input(LocalJobPath, std::ios::binary);
        if (!input)
            return;
        std::string bytes(MaximumLocalJobBytes + 1, '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        if (input.bad() || bytes.size() > MaximumLocalJobBytes
            || !MessagePack::has_bounded_structure(bytes, 65536, MaximumLocalJobs, 16))
            return;
        const auto loaded = MessagePack::deserialize<std::vector<LocalJob>>(bytes);
        if (!loaded.has_value() || loaded.value().size() > MaximumLocalJobs)
            return;
        for (const auto& job : loaded.value()) {
            if (job.hash.size() != 64 || job.bytes == 0 || !job.dataset.has_value()
                || job.dataset.value().bytes != job.bytes
                || !storage_dataset_id(job.owner, job.dataset.value()).has_value()
                || !Dfs::Path::file_path(job.owner, job.file_id).has_value()
                || (!job.pending.empty() && job.pending.size() != 64))
                continue;
            auto            restored = job;
            std::error_code error;
            if (!std::filesystem::is_regular_file(index_path(job), error) || error)
                restored.indexed = false;
            jobs.insert_or_assign(job_key(job), std::move(restored));
        }
    }

    void offer(const ActorId& owner, const Dfs::DirRow& row) {
        if (node->consensus() == nullptr || !node->consensus()->native_mining_enabled()
            || row.type != Dfs::FileType::File || row.state != Dfs::FileState::Ready || row.size == 0
            || row.hash.size() != 64 || !Dfs::Path::file_path(owner, row.file_id).has_value())
            return;
        LocalJob job { .owner = owner, .file_id = row.file_id, .hash = row.hash, .bytes = row.size };
        {
            std::lock_guard lock(mutex);
            if (stopped || !enabled)
                return;
            const auto key      = job_key(job);
            const auto existing = jobs.find(key);
            if (existing != jobs.end() && existing->second.bytes == job.bytes) {
                existing->second.available = true;
                return;
            }
            for (auto& [_, prior] : jobs)
                if (prior.owner == owner && prior.file_id == row.file_id && prior.hash != row.hash)
                    prior.available = false;
            if (existing == jobs.end() && jobs.size() >= MaximumLocalJobs)
                return;
            jobs.insert_or_assign(key, std::move(job));
        }
        prepare_next();
    }

    void missing(const ActorId& owner, const std::string& file_id) {
        {
            std::lock_guard lock(mutex);
            if (stopped)
                return;
            for (auto& [_, job] : jobs)
                if (job.owner == owner && job.file_id == file_id)
                    job.available = false;
        }
        save();
        advance();
    }

    void prepare_next() {
        if (node->consensus() == nullptr || !node->consensus()->native_mining_enabled())
            return;
        LocalJob selected;
        {
            std::lock_guard lock(mutex);
            if (stopped || !enabled || preparing)
                return;
            const auto now = std::chrono::steady_clock::now();
            for (const auto& [key, job] : jobs) {
                const auto retry = retry_after.find(key);
                if (job.available && !job.indexed && (retry == retry_after.end() || retry->second <= now)) {
                    selected = job;
                    break;
                }
            }
            if (selected.file_id.empty())
                return;
            preparing = true;
        }
        const auto self = shared_from_this();
        node->post_compute([self, selected] {
            const auto prepared = [&]() -> std::expected<StorageDataset, ConsensusError> {
                const auto path = Dfs::Path::file_path(selected.owner, selected.file_id);
                if (!path.has_value())
                    return std::unexpected(ConsensusError::StorageUnavailable);
                if (path.value().file_size().value_or(0) != selected.bytes)
                    return std::unexpected(ConsensusError::InvalidRoot);
                std::error_code error;
                std::filesystem::create_directories(index_path(selected).parent_path(), error);
                if (error)
                    return std::unexpected(ConsensusError::StorageFailure);
                auto          read = file_reader(path.value().native(), selected.bytes);
                blake3_hasher hasher;
                blake3_hasher_init(&hasher);
                std::uint64_t next_chunk = 0;
                const auto chunks = selected.bytes / StorageChunkBytes + (selected.bytes % StorageChunkBytes != 0);
                return write_storage_index(index_path(selected),
                                           selected.bytes,
                                           [&](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
                                               {
                                                   std::lock_guard lock(self->mutex);
                                                   if (self->stopped || !self->enabled)
                                                       return std::unexpected(ConsensusError::NotReady);
                                               }
                                               if (index != next_chunk++)
                                                   return std::unexpected(ConsensusError::InvalidProof);
                                               const auto chunk = read(index);
                                               if (!chunk.has_value())
                                                   return std::unexpected(chunk.error());
                                               blake3_hasher_update(&hasher,
                                                                    chunk.value().data(),
                                                                    chunk.value().size());
                                               if (next_chunk == chunks) {
                                                   std::array<std::uint8_t, BLAKE3_OUT_LEN> digest;
                                                   blake3_hasher_finalize(&hasher, digest.data(), digest.size());
                                                   if (fmt::format("{:02x}", fmt::join(digest, ""))
                                                           != selected.hash
                                                       || path.value().file_size().value_or(0) != selected.bytes)
                                                       return std::unexpected(ConsensusError::InvalidRoot);
                                               }
                                               return chunk;
                                           });
            }();
            {
                std::lock_guard lock(self->mutex);
                if (self->stopped)
                    return;
            }
            self->node->post_storage([self, selected, prepared] {
                {
                    std::lock_guard lock(self->mutex);
                    self->preparing = false;
                    if (self->stopped)
                        return;
                    const auto key   = job_key(selected);
                    const auto found = self->jobs.find(key);
                    if (found != self->jobs.end() && found->second.hash == selected.hash) {
                        if (prepared.has_value()) {
                            found->second.dataset = prepared.value();
                            found->second.indexed = true;
                            self->retry_after.erase(key);
                        } else {
                            found->second.indexed = false;
                            if (prepared.error() == ConsensusError::InvalidRoot)
                                found->second.available = false;
                            self->retry_after[key] = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                        }
                    }
                }
                self->save();
                self->advance();
                self->prepare_next();
            });
        });
    }

    std::chrono::steady_clock::time_point last_discovery { };
    void                                  discover() {
        if (node->consensus() == nullptr || !node->consensus()->native_mining_enabled())
            return;
        {
            std::lock_guard lock(mutex);
            if (stopped || !enabled
                || std::chrono::steady_clock::now() - last_discovery < std::chrono::seconds(60))
                return;
            last_discovery = std::chrono::steady_clock::now();
        }
        if (node->consensus() == nullptr || !node->consensus()->native_mining_enabled())
            return;
        const auto database = node->dfs()->dirs_manager().get_db_instance();
        if (!database)
            return;
        const auto rows = database->select(
            "SELECT * FROM ActorsFiles WHERE type = 10 AND state = 2 ORDER BY owner_id,file_id LIMIT 1024",
            "ActorsFiles");
        for (const auto& row : rows) {
            const auto file = Utils::from_dbrow<Dfs::DirRow>(row);
            if (file.has_value())
                offer(file.value().owner_id, file.value());
        }
        prepare_next();
    }

    // Job changes and explicit requests always run a pass. Consensus progress runs one
    // at once after a new certificate or finalized checkpoint; otherwise at most once
    // per ProgressRetryInterval, which still lets queued submissions follow admission
    // that expiry frees without a certificate.
    void advance(bool consensus_progress = false) {
        std::unique_lock progress_lock(progress_mutex, std::try_to_lock);
        if (!progress_lock.owns_lock())
            return;
        if (!node->account_controller()->has_current_profile() || node->consensus() == nullptr)
            return;
        const auto metrics = node->consensus()->metrics();
        const auto now     = std::chrono::steady_clock::now();
        if (consensus_progress && metrics.certificates == progress_certificates
            && metrics.finalized == progress_finalized && now - progress_attempt < ProgressRetryInterval)
            return;
        progress_certificates = metrics.certificates;
        progress_finalized    = metrics.finalized;
        progress_attempt      = now;
        std::map<std::string, LocalJob> local;
        {
            std::lock_guard lock(mutex);
            if (stopped || !enabled)
                return;
            local = jobs;
        }
        const auto provider = node->account_controller()->system_actor();
        for (std::size_t attempt = 0; attempt < 8; ++attempt) {
            const auto repaired = node->consensus()->repair_local_nonce_gap(provider);
            if (!repaired.has_value() || !repaired.value())
                break;
        }
        const auto work     = node->consensus()->mining_work_state();
        if (!work.has_value())
            return;
        std::map<std::string, std::pair<std::string, LocalJob>> datasets;
        for (auto& [key, job] : local) {
            if (!job.dataset.has_value())
                continue;
            const auto identity = storage_dataset_id(work.value().network, job.dataset.value());
            if (!identity.has_value())
                continue;
            const auto source = Dfs::Path::file_path(job.owner, job.file_id);
            job.available =
                job.available && source.has_value() && source.value().file_size().value_or(0) == job.bytes;
            const auto found = datasets.find(identity.value());
            if (found == datasets.end() || (!found->second.second.available && job.available)
                || (job.available && job.indexed && !found->second.second.indexed))
                datasets.insert_or_assign(identity.value(), std::pair { key, job });
        }
        std::vector<std::string> retired;
        for (const auto& [key, job] : local) {
            if (job.available)
                continue;
            if (!job.pending.empty()) {
                const auto receipt = node->consensus()->intent_receipt(job.pending);
                if (!receipt.has_value()
                    || (receipt.value().has_value()
                        && (receipt.value().value().status == IntentStatus::Accepted
                            || receipt.value().value().status == IntentStatus::Certified)))
                    continue;
            }
            bool needed = false;
            if (job.dataset.has_value()) {
                const auto id           = storage_dataset_id(work.value().network, job.dataset.value()).value();
                const auto registration = work.value().registrations.find(id);
                const auto source       = datasets.find(id);
                needed                  = registration != work.value().registrations.end()
                                          && registration->second.providers.contains(provider.id().to_string())
                                          && (source == datasets.end() || !source->second.second.available);
            }
            if (!needed)
                retired.push_back(key);
        }
        std::vector<std::filesystem::path> obsolete_indexes;
        {
            std::lock_guard lock(mutex);
            if (!preparing)
                for (const auto& key : retired) {
                    const auto found = jobs.find(key);
                    if (found != jobs.end() && !found->second.available) {
                        obsolete_indexes.push_back(index_path(found->second));
                        jobs.erase(found);
                        retry_after.erase(key);
                    }
                }
        }
        for (const auto& path : obsolete_indexes) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        if (!obsolete_indexes.empty())
            save();
        local.clear();
        for (const auto& [_, entry] : datasets)
            local.insert(entry);
        std::size_t submissions       = 0;
        bool        admission_blocked = false;
        const auto  submit            = [&](const std::string& key,
                                            LocalJob&          job,
                                            IntentOperation    operation,
                                            const auto&        value,
                                            std::uint64_t      epoch = 0) {
            if (admission_blocked || submissions >= MaximumSubmissionsPerProgress)
                return;
            const auto accepted =
                node->consensus()->submit_mining_request(operation,
                                                         Utils::to_base64(MessagePack::serialize(value)),
                                                         provider,
                                                         work.value());
            if (!accepted.has_value()) {
                admission_blocked = accepted.error() == ConsensusError::PoolFull
                                    || accepted.error() == ConsensusError::DataUnavailable
                                    || accepted.error() == ConsensusError::NotReady;
                return;
            }
            job.pending        = accepted.value();
            job.provider       = provider.id();
            job.operation      = operation;
            job.epoch          = epoch;
            last_submitted_job = key;
            {
                std::lock_guard lock(mutex);
                const auto      found = jobs.find(key);
                if (found != jobs.end() && found->second.hash == job.hash) {
                    // A removal or index rebuild can complete while consensus accepts the request.
                    found->second.pending   = job.pending;
                    found->second.provider  = job.provider;
                    found->second.operation = job.operation;
                    found->second.epoch     = job.epoch;
                }
            }
            ++submissions;
        };
        auto cursor = local.upper_bound(last_submitted_job);
        for (std::size_t visited = 0; visited < local.size(); ++visited) {
            if (admission_blocked)
                break;
            if (cursor == local.end())
                cursor = local.begin();
            auto& [key, job] = *cursor++;
            if (!job.dataset.has_value())
                continue;
            const auto identity = storage_dataset_id(work.value().network, job.dataset.value());
            if (!identity.has_value())
                continue;
            const auto registration = work.value().registrations.find(identity.value());
            const bool registered   = registration != work.value().registrations.end()
                                      && registration->second.providers.contains(provider.id().to_string());
            if (!job.pending.empty()) {
                const auto receipt   = node->consensus()->intent_receipt(job.pending);
                bool       reflected = job.operation == IntentOperation::StorageRegister && registered;
                if (job.operation == IntentOperation::StorageUnregister)
                    reflected = !registered;
                if (job.operation == IntentOperation::StorageProof) {
                    const auto epoch = work.value().epochs.find(job.epoch);
                    if (epoch == work.value().epochs.end())
                        reflected = true;
                    else {
                        const auto dataset = epoch->second.datasets.find(identity.value());
                        reflected          = dataset != epoch->second.datasets.end()
                                             && dataset->second.accepted.contains(job.provider.to_string());
                    }
                }
                const bool pending = receipt.has_value() && receipt.value().has_value()
                                     && (receipt.value().value().status == IntentStatus::Accepted
                                         || receipt.value().value().status == IntentStatus::Certified);
                if (!reflected && pending)
                    continue;
                job.pending.clear();
            }
            const auto path = Dfs::Path::file_path(job.owner, job.file_id);
            const bool present =
                job.available && path.has_value() && path.value().file_size().value_or(0) == job.bytes;
            if (!present) {
                if (registered)
                    submit(key, job, IntentOperation::StorageUnregister, identity.value());
                continue;
            }
            if (!job.indexed)
                continue;
            bool proof_sent = false;
            for (const auto& [epoch_id, epoch] : work.value().epochs) {
                const auto dataset = epoch.datasets.find(identity.value());
                if (!epoch.challenge.has_value() || dataset == epoch.datasets.end()
                    || !dataset->second.providers.contains(provider.id().to_string())
                    || dataset->second.accepted.contains(provider.id().to_string()))
                    continue;
                const auto schedule = mining_epoch_schedule(epoch_id);
                if (!schedule.has_value() || work.value().section > schedule.value().proof_last_section)
                    continue;
                const auto proof = make_storage_proof_from_index(index_path(job),
                                                                 work.value().network,
                                                                 provider.id(),
                                                                 job.dataset.value(),
                                                                 epoch.challenge.value(),
                                                                 file_reader(path.value().native(), job.bytes));
                if (!proof.has_value()) {
                    {
                        std::lock_guard lock(mutex);
                        const auto      found = jobs.find(key);
                        if (found != jobs.end()) {
                            found->second.indexed = false;
                            retry_after.erase(key);
                        }
                    }
                    job.indexed = false;
                    prepare_next();
                    break;
                }
                submit(key,
                       job,
                       IntentOperation::StorageProof,
                       MiningProofSubmission { epoch_id, identity.value(), proof.value() },
                       epoch_id);
                proof_sent = !job.pending.empty();
                break;
            }
            if (job.indexed && !registered && !proof_sent)
                submit(key, job, IntentOperation::StorageRegister, job.dataset.value());
            if (submissions >= MaximumSubmissionsPerProgress)
                break;
        }
        if (submissions != 0)
            save();
    }
};

DataMiningManager::DataMiningManager(ExtraChain::Core::ExtraChainNode* node)
    : work_(std::make_shared<Work>(node)) {
    work_->enabled = node->runtime_profile() == RuntimeProfile::FullNode;
    work_->restore();
    const std::weak_ptr<Work> weak    = work_;
    const auto                offered = [weak](const ActorId& owner, const Dfs::DirRow& row) {
        if (const auto work = weak.lock())
            work->offer(owner, row);
    };
    work_->connections.emplace_back(node->dfs()->stored_event().subscribe(offered));
    work_->connections.emplace_back(node->dfs()->downloaded_event().subscribe(offered));
    work_->connections.emplace_back(node->dfs()->updated_event().subscribe(offered));
    const auto removed = [weak](const ActorId& owner, const std::string& file_id) {
        if (const auto work = weak.lock())
            work->missing(owner, file_id);
    };
    work_->connections.emplace_back(node->dfs()->removed_event().subscribe(removed));
    work_->connections.emplace_back(node->dfs()->local_removed_event().subscribe(removed));
}

DataMiningManager::~DataMiningManager() {
    prepare_shutdown();
}
void DataMiningManager::request_reward() {
    const auto work = work_;
    work->node->post_storage([work] {
        work->discover();
        work->advance();
    });
}
void DataMiningManager::consensus_progress() {
    work_->discover();
    work_->prepare_next();
    work_->advance(true);
}
void DataMiningManager::set_enabled(bool enabled) {
    {
        std::lock_guard lock(work_->mutex);
        work_->enabled = enabled;
        if (enabled) {
            work_->retry_after.clear();
            work_->last_discovery = { };
        }
    }
    if (enabled)
        request_reward();
}
void DataMiningManager::prepare_shutdown() {
    std::lock_guard lock(work_->mutex);
    work_->stopped = true;
    work_->connections.clear();
}
bool DataMiningManager::network_request_coin_reward(const Dfs::Reward::RequestReward&, const Responder&) {
    return false;
}
