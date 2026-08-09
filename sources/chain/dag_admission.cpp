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

    constexpr std::size_t AdmissionBatchSize  = 64;
    constexpr std::size_t AdmissionQueueLimit = 4096;
    constexpr std::size_t AdmissionPeerLimit  = 256;
    constexpr auto        AdmissionDelay      = std::chrono::milliseconds(5);

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

    explicit AdmissionState(Dag *owner_value)
        : owner(owner_value)
        , worker([this](std::stop_token token) {
            run(token);
        }) {
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
            return queue.empty() && !processing;
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

    void process_batch(const std::vector<std::shared_ptr<Request>> &requests) {
        std::map<SectionId, Section>              sections;
        std::set<Transaction>                     pending;
        std::unordered_map<NodeId, std::uint64_t> reservations;
        std::vector<std::shared_ptr<Request>>     accepted;
        auto                                      validation_frontier = owner->current_section_;

        for (const auto &request : requests) {
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
                result = owner->prove_transaction(transaction,
                                                  section->second.transactions,
                                                  &pending,
                                                  &validation_frontier);
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
        {
            std::lock_guard lock(owner->pack_hot_cache_mutex_);
            for (const auto &[id, payload] : payloads)
                owner->pack_hot_cache_.insert_or_assign(id, payload);
            while (owner->pack_hot_cache_.size() > PACK_HOT_CACHE_LIMIT)
                owner->pack_hot_cache_.erase(std::prev(owner->pack_hot_cache_.end()));
        }

        owner->cache_.check_and_update_cache_thread(last);
        for (const auto &[id, section] : sections) {
            if (!payloads.contains(id))
                continue;
            if (owner->chain_index_enabled_ && owner->chain_index_)
                owner->chain_index_->on_section_written(section);
            if (owner->control_index_ && is_aligned20(id) && !section.control.has_value())
                owner->control_index_->erase(id);
        }
        for (const auto &request : accepted)
            owner->cache_.apply_live_transaction(request->transaction);
        {
            std::lock_guard lock(owner->last_txs_mutex_);
            for (const auto &[sender, timestamp] : reservations)
                owner->last_txs_.insert_or_assign(sender, timestamp);
        }

        owner->update_range();
        owner->try_pack_hot();
        for (const auto &request : accepted) {
            send_result(*request, TransactionProveError::NoError);
            owner->check_self(request->transaction);
            complete(request->completion, {}, true);
        }
    }

    void run(std::stop_token token) {
        IsAdmissionWorker = true;
        while (true) {
            std::vector<std::shared_ptr<Request>> requests;
            bool                                  batch = false;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] {
                    return stopping || token.stop_requested() || !queue.empty();
                });
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
    bool                                         processing = false;
    bool                                         accepting  = false;
    bool                                         stopping   = false;
    std::jthread                                 worker;
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
