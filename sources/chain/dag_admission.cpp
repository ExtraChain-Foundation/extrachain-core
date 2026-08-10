/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "chain/dag.h"

#include <condition_variable>
#include <deque>
#include <future>
#include <thread>

#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"
#include "network/wire_format.h"
#include "utils/exc_utils.h"

namespace {

    constexpr std::size_t AdmissionBatchSize           = 64;
    constexpr std::size_t AdmissionQueueLimit          = 4096;
    constexpr std::size_t AdmissionPeerLimit           = 256;
    constexpr auto        AdmissionDelay               = std::chrono::milliseconds(5);
    constexpr auto        AdmissionCacheIdleDelay      = std::chrono::milliseconds(500);
    constexpr int         AdmissionCacheUpdateInterval = 160;
    static_assert(AdmissionCacheUpdateInterval + CACHE_LAG_SECTIONS < HOT_PACK_LAG);

    thread_local bool IsAdmissionWorker = false;

    std::string peer_key(const Responder &responder) {
        if (!responder.identifiers().empty())
            return *responder.identifiers().begin();
        if (!responder.ip().empty())
            return responder.ip();
        return "<local>";
    }

} // namespace

struct Dag::AdmissionState {
    struct Request {
        Transaction                           transaction;
        Responder                             responder;
        AdmissionCompletion                   completion;
        std::string                           peer;
        std::chrono::steady_clock::time_point queued_at;
    };

    struct DerivedBatch {
        std::map<SectionId, Section> sections;
        std::vector<Transaction>     local_transactions;
    };

    explicit AdmissionState(Dag *owner_value)
        : owner(owner_value) {
        const auto worker_count = owner->node->runtime_limits().admission_prevalidation_workers;
        prevalidation_workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            prevalidation_workers.emplace_back([this](std::stop_token token) {
                run_prevalidation(token);
            });
        }
        worker         = std::jthread([this](std::stop_token token) {
            run(token);
        });
        derived_worker = std::jthread([this](std::stop_token token) {
            run_derived(token);
        });
    }

    ~AdmissionState() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        worker.request_stop();
        condition.notify_all();
        if (worker.joinable())
            worker.join();
        {
            std::lock_guard lock(prevalidation_mutex);
            prevalidation_stopping = true;
        }
        for (auto &prevalidation_worker : prevalidation_workers)
            prevalidation_worker.request_stop();
        prevalidation_condition.notify_all();
        for (auto &prevalidation_worker : prevalidation_workers) {
            if (prevalidation_worker.joinable())
                prevalidation_worker.join();
        }
        {
            std::lock_guard lock(derived_mutex);
            derived_stopping = true;
        }
        derived_worker.request_stop();
        derived_condition.notify_all();
        if (derived_worker.joinable())
            derived_worker.join();
    }

    bool submit(const Transaction &transaction, const Responder &responder, AdmissionCompletion completion) {
        const auto source = peer_key(responder);
        bool       accepted;
        {
            std::lock_guard lock(mutex);
            auto           &count = peer_counts[source];
            accepted = accepting && !stopping && queue.size() < AdmissionQueueLimit && count < AdmissionPeerLimit;
            if (!accepted) {
                if (count == 0)
                    peer_counts.erase(source);
            } else {
                ++count;
                queue.push_back(
                    std::make_shared<Request>(Request { .transaction = transaction,
                                                        .responder   = responder,
                                                        .completion  = std::move(completion),
                                                        .peer        = source,
                                                        .queued_at   = std::chrono::steady_clock::now() }));
            }
        }
        if (!accepted) {
            complete(completion, std::unexpected(TransactionProveError::AdmissionBusy), false);
            return false;
        }
        condition.notify_one();
        return true;
    }

    void set_accepting(bool value) {
        std::lock_guard lock(mutex);
        accepting = value;
    }

    void flush() {
        if (IsAdmissionWorker)
            return;
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] {
            return queue.empty() && !processing && !cache_catchup_pending;
        });
        lock.unlock();
        std::unique_lock derived_lock(derived_mutex);
        derived_condition.wait(derived_lock, [this] {
            return derived_queue.empty() && !derived_processing;
        });
    }

private:
    static void complete(const AdmissionCompletion                 &completion,
                         std::expected<void, TransactionProveError> result,
                         bool                                       should_forward) {
        if (!completion)
            return;
        try {
            completion(std::move(result), should_forward);
        } catch (const std::exception &error) {
            eWarning("[Dag] Admission callback failed: {}", error.what());
        } catch (...) {
            eWarning("[Dag] Admission callback failed");
        }
    }

    void send_result(const Request &request, TransactionProveError result) const {
        if (request.responder.empty())
            return;
        request.responder.send_response(TransactionResult { .section_id = request.transaction.section(),
                                                            .hash       = request.transaction.hash(),
                                                            .result     = result },
                                        MessageType::DagTransactionResult,
                                        SendMode::Focused,
                                        MessageStatus::Response);
    }

    bool can_batch(const Request &request) const {
        return owner->status_ == DagStatus::Ready && owner->mode_ == DagMode::Full
               && !is_contract_transaction(request.transaction.type())
               && request.transaction.type() != TransactionType::Genesis
               && request.transaction.type() != TransactionType::Balance
               && request.transaction.section() > owner->current_section_;
    }

    void finish(const std::vector<std::shared_ptr<Request>> &requests) {
        std::lock_guard lock(mutex);
        for (const auto &request : requests) {
            auto count = peer_counts.find(request->peer);
            if (count != peer_counts.end() && --count->second == 0)
                peer_counts.erase(count);
        }
        processing = false;
        condition.notify_all();
    }

    TransactionProveError rate_limit(const Transaction                               &transaction,
                                     const std::unordered_map<NodeId, std::uint64_t> &reservations) const {
        if (transaction.type() != TransactionType::Regular)
            return TransactionProveError::NoError;
        const NodeId sender { .actor_id = transaction.sender(), .node_identifier = "" };
        const auto   now      = Utils::current_date_ms();
        auto         reserved = reservations.find(sender);
        if (reserved != reservations.end() && now - reserved->second < 4500)
            return TransactionProveError::TooOften;
        std::lock_guard lock(owner->last_txs_mutex_);
        auto            stored = owner->last_txs_.find(sender);
        return stored != owner->last_txs_.end() && now - stored->second < 4500 ? TransactionProveError::TooOften
                                                                               : TransactionProveError::NoError;
    }

    void reject(const std::shared_ptr<Request> &request, TransactionProveError result) {
        send_result(*request, result);
        complete(request->completion, std::unexpected(result), false);
    }

    void enqueue_derived(DerivedBatch batch) {
        const auto       section_count = batch.sections.size();
        const auto       limit = std::max(AdmissionBatchSize, owner->node->runtime_limits().derived_sections);
        std::unique_lock lock(derived_mutex);
        derived_condition.wait(lock, [&] {
            return derived_stopping || derived_section_count + section_count <= limit;
        });
        if (derived_stopping)
            return;
        derived_section_count += section_count;
        derived_queue.push_back(std::move(batch));
        lock.unlock();
        derived_condition.notify_one();
    }

    void process_derived(const DerivedBatch &batch) {
        if (owner->chain_index_enabled_ && owner->chain_index_) {
            for (const auto &entry : batch.sections)
                owner->chain_index_->on_section_written(entry.second);
        }
        for (const auto &transaction : batch.local_transactions)
            owner->check_self(transaction);
    }

    void run_prevalidation(std::stop_token token) {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock lock(prevalidation_mutex);
                prevalidation_condition.wait(lock, [&] {
                    return prevalidation_stopping || token.stop_requested() || !prevalidation_jobs.empty();
                });
                if ((prevalidation_stopping || token.stop_requested()) && prevalidation_jobs.empty())
                    return;
                job = std::move(prevalidation_jobs.front());
                prevalidation_jobs.pop_front();
            }
            job();
        }
    }

    std::vector<TransactionValidationFacts> prevalidate(const std::vector<std::shared_ptr<Request>> &requests) {
        std::unordered_map<ActorId, Actor<KeyPublic>> actors;
        actors.reserve(requests.size() * 2);
        for (const auto &request : requests) {
            const auto &transaction = request->transaction;
            if (!transaction.sender().is_zero() && !actors.contains(transaction.sender()))
                actors.emplace(transaction.sender(),
                               owner->node->actor_index()->read_actor_old(transaction.sender()));
            if (transaction.type() != TransactionType::Burn && !transaction.receiver().is_zero()
                && !actors.contains(transaction.receiver())) {
                actors.emplace(transaction.receiver(),
                               owner->node->actor_index()->read_actor_old(transaction.receiver()));
            }
        }

        std::vector<TransactionValidationFacts> facts(requests.size());
        const auto                              prevalidate_range = [&](std::size_t first, std::size_t last) {
            for (auto index = first; index < last; ++index) {
                const auto &transaction = requests[index]->transaction;
                auto       &result      = facts[index];
                const auto  stored_hash = transaction.hash();
                result.hash_valid =
                    stored_hash == transaction.calculate_hash_hex() || stored_hash == transaction.calculate_hash();

                const auto sender = actors.find(transaction.sender());
                result.sender_exists = sender != actors.end() && !sender->second.empty();
                if (result.sender_exists.value() && !transaction.signature().empty()) {
                    const auto signature = sender->second.key().verify(stored_hash, transaction.signature());
                    result.signature_valid = signature.has_value() && signature.value();
                } else {
                    result.signature_valid = false;
                }

                if (transaction.type() != TransactionType::Burn && !transaction.receiver().is_zero()) {
                    const auto receiver = actors.find(transaction.receiver());
                    result.receiver_exists = receiver != actors.end() && !receiver->second.empty();
                }
            }
        };

        if (prevalidation_workers.empty() || requests.size() < 2) {
            prevalidate_range(0, requests.size());
            return facts;
        }

        const auto                     worker_count = std::min(prevalidation_workers.size(), requests.size());
        std::vector<std::future<void>> completed;
        completed.reserve(worker_count);
        {
            std::lock_guard lock(prevalidation_mutex);
            for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
                auto promise = std::make_shared<std::promise<void>>();
                completed.push_back(promise->get_future());
                const auto first = requests.size() * worker_index / worker_count;
                const auto last  = requests.size() * (worker_index + 1) / worker_count;
                prevalidation_jobs.emplace_back([&, first, last, promise] {
                    try {
                        prevalidate_range(first, last);
                        promise->set_value();
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                });
            }
        }
        prevalidation_condition.notify_all();
        for (auto &future : completed)
            future.get();
        return facts;
    }

    void process_batch(const std::vector<std::shared_ptr<Request>> &requests) {
        const auto                                prevalidated = prevalidate(requests);
        std::map<SectionId, Section>              sections;
        std::set<Transaction>                     pending;
        std::unordered_map<NodeId, std::uint64_t> reservations;
        std::vector<std::shared_ptr<Request>>     accepted;
        auto                                      validation_frontier = owner->current_section_;

        for (std::size_t request_index = 0; request_index < requests.size(); ++request_index) {
            const auto &request     = requests[request_index];
            const auto &transaction = request->transaction;

            auto section = sections.find(transaction.section());
            if (section == sections.end()) {
                section = sections
                              .emplace(transaction.section(),
                                       Section { .id = transaction.section(), .transactions = {} })
                              .first;
            }

            auto result = rate_limit(transaction, reservations);
            if (result == TransactionProveError::NoError)
                result = owner->prove_transaction_with_facts(transaction,
                                                             section->second.transactions,
                                                             &pending,
                                                             &validation_frontier,
                                                             &prevalidated[request_index]);
            if (result != TransactionProveError::NoError) {
                reject(request, result);
                continue;
            }
            if (!section->second.transactions.insert(transaction).second) {
                reject(request, TransactionProveError::Duplicate);
                continue;
            }

            pending.insert(transaction);
            accepted.push_back(request);
            validation_frontier = std::max(validation_frontier, transaction.section());
            if (transaction.type() == TransactionType::Regular) {
                reservations.insert_or_assign(NodeId { .actor_id = transaction.sender(), .node_identifier = "" },
                                              Utils::current_date_ms());
            }
        }

        if (accepted.empty())
            return;

        std::map<SectionId, std::string> payloads;
        {
            WireFormat::Scope scope(WireFormat::Mode::Canonical);
            for (const auto &[id, section] : sections) {
                const bool changed = std::ranges::any_of(accepted, [&](const auto &request) {
                    return request->transaction.section() == id;
                });
                if (changed)
                    payloads.emplace(id, Json::serialize(section));
            }
        }

        auto first = owner->first_saved_section_;
        first      = first == SectionId(-1) ? payloads.begin()->first : std::min(first, payloads.begin()->first);
        const auto last   = std::max(owner->current_section_, payloads.rbegin()->first);
        const bool stored = owner->hot_section_store_ && owner->hot_section_store_->is_open()
                            && owner->hot_section_store_->commit_batch(payloads, std::pair { first, last });
        if (!stored) {
            for (const auto &request : accepted)
                reject(request, TransactionProveError::NoSectionAdded);
            return;
        }

        owner->first_saved_section_ = first;
        owner->current_section_     = last;

        const auto cached_section = owner->cache_.section();
        if (cached_section == SectionId(-1) || last - cached_section >= SectionId(AdmissionCacheUpdateInterval)) {
            owner->cache_.check_and_update_cache_thread(last);
        }
        std::vector<Transaction> accepted_transactions;
        accepted_transactions.reserve(accepted.size());
        for (const auto &request : accepted)
            accepted_transactions.push_back(request->transaction);
        owner->cache_.apply_live_transactions(accepted_transactions);
        {
            std::lock_guard lock(owner->last_txs_mutex_);
            for (const auto &[sender, timestamp] : reservations)
                owner->last_txs_.insert_or_assign(sender, timestamp);
        }

        owner->update_range();
        owner->try_pack_hot();
        std::erase_if(sections, [&](const auto &entry) {
            return !payloads.contains(entry.first);
        });
        const auto               local_actor_ids = owner->node->account_controller()->accounts_ids();
        std::vector<Transaction> local_transactions;
        for (const auto &transaction : accepted_transactions) {
            const auto local = std::ranges::any_of(local_actor_ids, [&](const ActorId &actor_id) {
                return transaction.sender() == actor_id || transaction.receiver() == actor_id;
            });
            if (local)
                local_transactions.push_back(transaction);
        }
        enqueue_derived(
            DerivedBatch { .sections = std::move(sections), .local_transactions = std::move(local_transactions) });
        {
            std::lock_guard lock(mutex);
            cache_catchup_pending = true;
            cache_catchup_due     = std::chrono::steady_clock::now() + AdmissionCacheIdleDelay;
        }
        condition.notify_all();
        for (const auto &request : accepted) {
            send_result(*request, TransactionProveError::NoError);
            complete(request->completion, {}, true);
        }
    }

    void run_derived(std::stop_token token) {
        while (true) {
            DerivedBatch batch;
            {
                std::unique_lock lock(derived_mutex);
                derived_condition.wait(lock, [&] {
                    return derived_stopping || token.stop_requested() || !derived_queue.empty();
                });
                if ((derived_stopping || token.stop_requested()) && derived_queue.empty())
                    break;
                derived_processing = true;
                batch              = std::move(derived_queue.front());
                derived_queue.pop_front();
            }

            try {
                process_derived(batch);
            } catch (const std::exception &error) {
                eWarning("[Dag] Derived processing failed: {}", error.what());
            } catch (...) {
                eWarning("[Dag] Derived processing failed");
            }

            {
                std::lock_guard lock(derived_mutex);
                derived_section_count -= batch.sections.size();
                derived_processing = false;
            }
            derived_condition.notify_all();
        }
        std::lock_guard lock(derived_mutex);
        derived_processing = false;
        derived_condition.notify_all();
    }

    void run(std::stop_token token) {
        IsAdmissionWorker = true;
        while (true) {
            std::vector<std::shared_ptr<Request>> requests;
            bool                                  batch = false;
            {
                std::unique_lock lock(mutex);
                while (queue.empty() && !stopping && !token.stop_requested()) {
                    if (!cache_catchup_pending) {
                        condition.wait(lock, [&] {
                            return stopping || token.stop_requested() || !queue.empty() || cache_catchup_pending;
                        });
                        continue;
                    }
                    const bool interrupted = condition.wait_until(lock, cache_catchup_due, [&] {
                        return stopping || token.stop_requested() || !queue.empty();
                    });
                    if (interrupted)
                        continue;
                    cache_catchup_pending = false;
                    processing            = true;
                    lock.unlock();
                    try {
                        owner->cache_.check_and_update_cache_thread(owner->current_section_);
                    } catch (const std::exception &error) {
                        eWarning("[Dag] Admission cache catch-up failed: {}", error.what());
                    } catch (...) {
                        eWarning("[Dag] Admission cache catch-up failed");
                    }
                    lock.lock();
                    processing = false;
                    condition.notify_all();
                }
                if ((stopping || token.stop_requested()) && queue.empty())
                    break;

                batch               = can_batch(*queue.front());
                const auto deadline = queue.front()->queued_at + AdmissionDelay;
                if (batch && queue.size() < AdmissionBatchSize && !stopping && !token.stop_requested()) {
                    condition.wait_until(lock, deadline, [&] {
                        return stopping || token.stop_requested() || queue.size() >= AdmissionBatchSize;
                    });
                }

                processing = true;
                requests.push_back(std::move(queue.front()));
                queue.pop_front();
                while (batch && requests.size() < AdmissionBatchSize && !queue.empty()
                       && can_batch(*queue.front())) {
                    requests.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
            }

            try {
                if (batch) {
                    process_batch(requests);
                } else {
                    auto result = owner->network_transaction_immediate(requests.front()->transaction,
                                                                       requests.front()->responder);
                    complete(requests.front()->completion, result, result.has_value());
                }
            } catch (const std::exception &error) {
                eWarning("[Dag] Admission processing failed: {}", error.what());
                for (const auto &request : requests)
                    complete(request->completion, std::unexpected(TransactionProveError::AdmissionBusy), false);
            } catch (...) {
                eWarning("[Dag] Admission processing failed");
                for (const auto &request : requests)
                    complete(request->completion, std::unexpected(TransactionProveError::AdmissionBusy), false);
            }
            finish(requests);
        }
        IsAdmissionWorker = false;
        std::lock_guard lock(mutex);
        processing = false;
        condition.notify_all();
    }

    Dag                                         *owner;
    std::mutex                                   mutex;
    std::condition_variable                      condition;
    std::deque<std::shared_ptr<Request>>         queue;
    std::unordered_map<std::string, std::size_t> peer_counts;
    bool                                         processing            = false;
    bool                                         accepting             = false;
    bool                                         stopping              = false;
    bool                                         cache_catchup_pending = false;
    std::chrono::steady_clock::time_point        cache_catchup_due;
    std::mutex                                   derived_mutex;
    std::condition_variable                      derived_condition;
    std::deque<DerivedBatch>                     derived_queue;
    std::size_t                                  derived_section_count = 0;
    bool                                         derived_processing    = false;
    bool                                         derived_stopping      = false;
    std::jthread                                 worker;
    std::jthread                                 derived_worker;
    std::mutex                                   prevalidation_mutex;
    std::condition_variable                      prevalidation_condition;
    std::deque<std::function<void()>>            prevalidation_jobs;
    std::vector<std::jthread>                    prevalidation_workers;
    bool                                         prevalidation_stopping = false;
};

std::shared_ptr<Dag::AdmissionState> Dag::create_admission_state(Dag *owner) {
    return std::make_shared<AdmissionState>(owner);
}

bool Dag::is_admission_worker() {
    return IsAdmissionWorker;
}

void Dag::set_admission_accepting(bool accepting) {
    if (admission_state_)
        admission_state_->set_accepting(accepting);
}

std::expected<void, TransactionProveError> Dag::network_transaction(const Transaction &transaction,
                                                                    const Responder   &responder) {
    if (is_admission_worker() || !admission_state_)
        return network_transaction_immediate(transaction, responder);

    auto promise = std::make_shared<std::promise<std::expected<void, TransactionProveError>>>();
    auto future  = promise->get_future();
    submit_network_transaction(transaction,
                               responder,
                               [promise](std::expected<void, TransactionProveError> result, bool) mutable {
                                   promise->set_value(std::move(result));
                               });
    return future.get();
}

bool Dag::submit_network_transaction(const Transaction  &transaction,
                                     const Responder    &responder,
                                     AdmissionCompletion completion) {
    if (!accepting_messages_.load()) {
        if (completion) {
            try {
                completion(std::unexpected(TransactionProveError::AdmissionBusy), false);
            } catch (const std::exception &error) {
                eWarning("[Dag] Admission callback failed: {}", error.what());
            } catch (...) {
                eWarning("[Dag] Admission callback failed");
            }
        }
        return false;
    }

    if (!admission_state_) {
        auto result = network_transaction_immediate(transaction, responder);
        if (completion) {
            try {
                completion(result, result.has_value());
            } catch (const std::exception &error) {
                eWarning("[Dag] Admission callback failed: {}", error.what());
            } catch (...) {
                eWarning("[Dag] Admission callback failed");
            }
        }
        return result.has_value();
    }
    return admission_state_->submit(transaction, responder, std::move(completion));
}

void Dag::flush_admission() {
    if (admission_state_)
        admission_state_->flush();
}
