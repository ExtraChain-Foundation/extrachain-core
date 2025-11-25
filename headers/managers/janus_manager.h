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

#include <boost/describe.hpp>

#include "chain/actor_id.h"
#include "dfs/dfs_controller.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"

class ExtraChainNode;
class DbConnector;
struct NodeId;
class BigNumberFloat;

namespace Dfs {
    struct FileLink;
    class CollectionTemplate;
}

/**
 * @struct JanusBidBase
 * @brief Base structure for any bid in Janus system
 *
 * Inherit from this to create application-specific bid types.
 * All derived types must use BOOST_DESCRIBE_STRUCT with JanusBidBase as base.
 */
struct JanusBidBase {
    std::uint64_t timestamp = 0;
    ActorId       actor;
    std::string   amount;
    std::string   message;
};
BOOST_DESCRIBE_STRUCT(JanusBidBase, (), (timestamp, actor, amount, message))

/**
 * @struct JanusItemBase
 * @brief Base structure for any item/task in Janus system
 *
 * Inherit from this to create application-specific item types.
 * All derived types must use BOOST_DESCRIBE_STRUCT with JanusItemBase as base.
 */
struct JanusItemBase {
    ActorId       owner_id;
    std::string   file_id;
    std::string   title;
    std::string   description;
    std::uint64_t created_at = 0;
};
BOOST_DESCRIBE_STRUCT(JanusItemBase, (), (owner_id, file_id, title, description, created_at))

/**
 * @class JanusManager
 * @brief Universal two-sided market/auction platform
 *
 * Provides infrastructure for building any two-sided marketplace:
 * - Freelance exchanges
 * - Auction platforms
 * - Computing marketplaces (Argentum)
 * - Service marketplaces
 *
 * Applications extend base structures (JanusBidBase, JanusItemBase) and use
 * template methods to work with their custom types.
 *
 * @see JanusBidBase
 * @see JanusItemBase
 */
class JanusManager {
public:
    JanusManager(ExtraChainNode *node);
    ~JanusManager() = default;

    /**
     * @brief Create a bid template with custom fields
     * @param template_name Unique name for the template
     * @param tmpl CollectionTemplate defining bid structure
     * @return true if template created successfully
     */
    bool create_bid_template(const std::string &template_name, const Dfs::CollectionTemplate &tmpl);

    /**
     * @brief Get existing bid template by name
     * @param template_name Name of the template to find
     * @return DirRow if found, nullopt otherwise
     */
    std::optional<Dfs::DirRow> get_bid_template(const std::string &template_name);

    /**
     * @brief Create a vector for items that accepts bids with specified template
     * @param vector_name Name for the new item vector
     * @param bid_template_name Name of the bid template to associate
     * @return DirRow on success, DfsFileStatus error otherwise
     */
    std::expected<Dfs::DirRow, DfsFileStatus> create_item_vector(const std::string &vector_name,
                                                                  const std::string &bid_template_name);

    /**
     * @brief Place a bid on an item (universal template method)
     * @tparam BidT Bid type - must be BOOST_DESCRIBE-enabled, should inherit from JanusBidBase
     * @param item_owner_id Owner of the item
     * @param item_file_id File ID of the item
     * @param bid Bid data (timestamp and actor will be set automatically)
     * @return Empty string on success, nullopt on failure
     */
    template<typename BidT>
    std::optional<std::string> place_bid(const ActorId     &item_owner_id,
                                         const std::string &item_file_id,
                                         BidT               bid);

    /**
     * @brief Create default Janus bid template (amount, message fields)
     * @param template_name Name for the template (default: "JanusBids")
     * @return true if template created successfully
     */
    bool create_default_bid_template(const std::string &template_name = "JanusBids");

    /**
     * @brief Get the ExtraChainNode instance
     * @return Pointer to the node
     */
    ExtraChainNode *get_node() const { return node; }

private:
    ExtraChainNode *node;
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename BidT>
std::optional<std::string> JanusManager::place_bid(const ActorId     &item_owner_id,
                                                   const std::string &item_file_id,
                                                   BidT               bid) {
    auto file_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), item_owner_id, item_file_id);

    if (!file_row.has_value()) {
        return std::nullopt;
    }

    if (file_row->state != Dfs::FileState::Ready) {
        return std::nullopt;
    }

    auto main_id = node->account_controller()->current_profile().main_id();

    // Set common fields
    bid.timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    bid.actor = main_id;

    auto res = node->dfs()->add_vector_row(item_owner_id, item_file_id, bid, main_id);
    if (!res) {
        return std::nullopt;
    }

    return "";
}
