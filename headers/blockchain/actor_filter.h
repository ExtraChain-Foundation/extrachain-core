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
#include <cmath>
#include <cstring>

#include "blockchain/actor_id.h"

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
    std::vector<ActorId> local_actors_;

    /// Number of buckets (256 = 1 byte for bucket index)
    static constexpr size_t BUCKET_COUNT = 256;

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

    /**
     * @brief Creates hash values for all buckets based on current local actors
     * @return BucketHashes structure containing hash values for all buckets
     *
     * This function distributes all local actors into their respective buckets
     * and computes a hash value for each bucket's contents.
     */
    BucketHashes create_bucket_hashes() const {
        // Distribute actors into buckets
        std::unordered_map<uint8_t, std::vector<std::string>> buckets;

        for (const auto& actor : local_actors_) {
            std::string actor_str    = actor.to_string();
            uint8_t     bucket_index = get_bucket_index(actor_str);
            buckets[bucket_index].push_back(actor_str);
        }

        // Compute hash for each bucket
        BucketHashes result;

        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            auto it = buckets.find(static_cast<uint8_t>(i));
            if (it != buckets.end()) {
                result.hashes[i] = compute_bucket_hash(it->second);
            }
        }

        return result;
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
        std::vector<ActorId> result;

        // Create a set of indices for fast checking
        std::unordered_set<uint8_t> indices(bucket_indices.begin(), bucket_indices.end());

        // Set for tracking already added actors (remove duplicates)
        std::unordered_set<std::string> added_actors;

        // Check each actor
        for (const auto& actor : local_actors_) {
            std::string actor_str = actor.to_string();

            // Skip if actor was already added
            if (added_actors.find(actor_str) != added_actors.end()) {
                continue;
            }

            uint8_t bucket_index = get_bucket_index(actor_str);

            if (indices.find(bucket_index) != indices.end()) {
                result.push_back(actor);
                added_actors.insert(actor_str);
            }
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
        local_actors_ = actors;
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
        // Set for fast checking of existing actors
        std::unordered_set<std::string> existing_actors;
        for (const auto& actor : local_actors_) {
            existing_actors.insert(actor.to_string());
        }

        // Add only unique actors
        for (const auto& actor : received_ids) {
            std::string actor_str = actor.to_string();
            if (existing_actors.find(actor_str) == existing_actors.end()) {
                local_actors_.push_back(actor);
                existing_actors.insert(actor_str);
            }
        }
    }
};