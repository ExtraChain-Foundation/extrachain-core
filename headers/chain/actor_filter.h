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

#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <set>
#include <utility>

#include "chain/actor_id.h"

/**
 * @class ActorSynchronizer
 * @brief Manages efficient synchronization of actor IDs between nodes in a distributed system
 *
 * ActorSynchronizer implements a bucket-based reconciliation algorithm that allows
 * two nodes to identify and exchange only the differing actor IDs, minimizing
 * network traffic during synchronization processes.
 */
class ActorSynchronizer {
private:
    /// Number of buckets (256 = 1 byte for bucket index)
    static constexpr size_t BUCKET_COUNT = 256;
    std::array<std::set<ActorId>, BUCKET_COUNT> actors_by_bucket_;

    /**
     * @brief Computes hash for an actor string using FNV-1a 64-bit algorithm
     * @param str Actor string to hash
     * @return 64-bit hash value
     */
    static uint64_t hash_actor(const std::string& str) {
        const uint64_t FNV_PRIME        = 1099511628211ULL;
        const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

        uint64_t hash = FNV_OFFSET_BASIS;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    /**
     * @brief Determines the bucket index for a given actor string
     * @param actorStr Actor string to categorize
     * @return 8-bit bucket index (0-255)
     */
    static uint8_t get_bucket_index(const std::string& actor_str) {
        uint64_t hash = hash_actor(actor_str);
        return static_cast<uint8_t>(hash % BUCKET_COUNT);
    }

    /**
     * @brief Calculates a hash for the contents of a bucket
     * @param bucketItems Vector of actor strings in the bucket
     * @return 64-bit hash representing the bucket contents
     *
     * The function produces a deterministic hash by:
     * 1. Sorting items for consistent ordering
     * 2. Removing duplicates
     * 3. Concatenating and hashing the combined string
     * Returns 0 for empty buckets.
     */
    static uint64_t compute_bucket_hash(const std::vector<std::string>& bucket_items) {
        // If bucket is empty
        if (bucket_items.empty()) {
            return 0;
        }

        // Sort for stable hashing
        std::vector<std::string> sorted_items = bucket_items;
        std::sort(sorted_items.begin(), sorted_items.end());

        // Remove duplicates
        auto last = std::unique(sorted_items.begin(), sorted_items.end());
        sorted_items.erase(last, sorted_items.end());

        // Combine all elements and hash
        std::string combined;
        for (const auto& item : sorted_items) {
            combined += item;
        }

        return hash_actor(combined);
    }

    /**
     * @struct BucketHashes
     * @brief Container for storing hash values of all buckets
     *
     * Initializes all hash values to zero by default.
     */
    struct BucketHashes {
        uint64_t hashes[BUCKET_COUNT];

        BucketHashes() {
            memset(hashes, 0, sizeof(hashes));
        }
    };

    mutable BucketHashes cached_hashes_;
    mutable std::bitset<BUCKET_COUNT> dirty_buckets_ = ~std::bitset<BUCKET_COUNT> {};

    /**
     * @brief Creates hash values for all buckets based on current local actors
     * @return BucketHashes structure containing hash values for all buckets
     *
     * This function distributes all local actors into their respective buckets
     * and computes a hash value for each bucket's contents.
     */
    BucketHashes create_bucket_hashes() const {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (!dirty_buckets_.test(i)) {
                continue;
            }
            std::vector<std::string> actors;
            actors.reserve(actors_by_bucket_[i].size());
            for (const auto& actor : actors_by_bucket_[i]) {
                actors.push_back(actor.to_string());
            }
            cached_hashes_.hashes[i] = compute_bucket_hash(actors);
            dirty_buckets_.reset(i);
        }
        return cached_hashes_;
    }

    /**
     * @brief Serializes bucket hashes for network transmission
     * @param hashes BucketHashes structure to serialize
     * @return Vector of bytes containing serialized data
     */
    static std::vector<uint8_t> serialize_bucket_hashes(const BucketHashes& hashes) {
        std::vector<uint8_t> result(sizeof(hashes.hashes));
        memcpy(result.data(), hashes.hashes, sizeof(hashes.hashes));
        return result;
    }

    /**
     * @brief Deserializes bucket hashes from received data
     * @param data Vector of bytes containing serialized bucket hashes
     * @return BucketHashes structure populated from the data
     */
    static BucketHashes deserialize_bucket_hashes(const std::vector<uint8_t>& data) {
        BucketHashes result;
        memcpy(result.hashes, data.data(), std::min(sizeof(result.hashes), data.size()));
        return result;
    }

    /**
     * @brief Identifies buckets with different hash values between local and remote
     * @param local_hashes Local bucket hashes
     * @param remote_hashes Remote bucket hashes
     * @return Vector of bucket indices that differ between local and remote
     */
    static std::vector<uint8_t> find_different_buckets(const BucketHashes& local_hashes,
                                                       const BucketHashes& remote_hashes) {
        std::vector<uint8_t> different_buckets;

        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (local_hashes.hashes[i] != remote_hashes.hashes[i]) {
                different_buckets.push_back(static_cast<uint8_t>(i));
            }
        }

        return different_buckets;
    }

    /**
     * @brief Retrieves all actors from specified buckets
     * @param bucket_indices Vector of bucket indices to retrieve actors from
     * @return Vector of unique ActorId objects from the specified buckets
     *
     * This function collects all actors that belong to the specified buckets
     * while ensuring that no duplicates are included in the result.
     */
    std::vector<ActorId> get_actors_from_buckets(const std::vector<uint8_t>& bucket_indices) const {
        std::vector<const ActorId*> selected;
        std::bitset<BUCKET_COUNT> added_buckets;
        for (uint8_t index : bucket_indices) {
            if (added_buckets.test(index)) {
                continue;
            }
            added_buckets.set(index);
            const auto& actors = actors_by_bucket_[index];
            for (const auto& actor : actors) {
                selected.push_back(&actor);
            }
        }
        // Sort references: moving ActorId values revalidates and reallocates their strings.
        std::sort(selected.begin(), selected.end(), [](const ActorId* left, const ActorId* right) {
            return *left < *right;
        });
        std::vector<ActorId> result;
        result.reserve(selected.size());
        for (const auto* actor : selected) {
            result.push_back(*actor);
        }
        return result;
    }

public:
    /**
     * @brief Default constructor
     */
    ActorSynchronizer() {
    }

    /**
     * @brief Sets the collection of local actors to be synchronized
     * @param actors Vector of ActorId objects to set as local actors
     */
    void set_actors(const std::vector<ActorId>& actors) {
        ActorSynchronizer replacement;
        replacement.apply_received_ids(actors);
        *this = std::move(replacement);
    }

    /**
     * @brief Creates a synchronization request for sending to another node
     * @return Vector of bytes containing serialized bucket hashes
     *
     * This function generates hash values for all buckets of local actors
     * and serializes them for transmission to a remote node.
     */
    std::vector<uint8_t> create_sync_request() {
        // Create bucket hashes for local actors
        auto bucket_hashes = create_bucket_hashes();

        // Serialize bucket hashes for transmission
        return serialize_bucket_hashes(bucket_hashes);
    }

    /**
     * @brief Processes a received synchronization request
     * @param request_data Vector of bytes containing serialized bucket hashes from remote node
     * @return Vector of ActorId objects that should be sent to the remote node
     *
     * This function compares the received bucket hashes with local bucket hashes
     * and returns the actors from buckets that differ, which should be sent
     * to the requesting node.
     */
    std::vector<ActorId> process_sync_request(const std::vector<uint8_t>& request_data) {
        // Deserialize received bucket hashes
        auto remote_bucket_hashes = deserialize_bucket_hashes(request_data);

        // Create hashes for local buckets
        auto local_bucket_hashes = create_bucket_hashes();

        // Find differing buckets
        auto different_buckets = find_different_buckets(local_bucket_hashes, remote_bucket_hashes);

        // Get actors from different buckets
        return get_actors_from_buckets(different_buckets);
    }

    /**
     * @brief Updates local actor collection with received actor IDs
     * @param received_ids Vector of ActorId objects received from a remote node
     *
     * This function adds the received actor IDs to the local collection,
     * ensuring that no duplicates are added.
     */
    void apply_received_ids(const std::vector<ActorId>& received_ids) {
        for (const auto& actor : received_ids) {
            const auto bucket = get_bucket_index(actor.to_string());
            if (actors_by_bucket_[bucket].insert(actor).second) {
                dirty_buckets_.set(bucket);
            }
        }
    }
};
