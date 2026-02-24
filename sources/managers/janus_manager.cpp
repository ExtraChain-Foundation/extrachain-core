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

#include "managers/janus_manager.h"

#include "dfs/dfs_controller.h"
#include "dfs/collection_template.h"
#include "managers/extrachain_node.h"

JanusManager::JanusManager(ExtraChainNode *node)
    : node(node) {
}

bool JanusManager::create_bid_template(const std::string &template_name, const Dfs::CollectionTemplate &tmpl) {
    auto system_actor_id = node->account_controller()->system_actor().id();
    auto template_res    = node->dfs()->store_template(system_actor_id, tmpl);

    if (!template_res.has_value()) {
        eCritical("Can't create bid template '{}': {}", template_name, template_res.error());
        return false;
    }

    return true;
}

std::optional<Dfs::DirRow> JanusManager::get_bid_template(const ActorId &owner_id, const std::string &template_name) {
    auto result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        node->dfs()->get_db_instance(), owner_id, Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE, template_name);

    if (result.has_value()) {
        return result.value();
    }

    return std::nullopt;
}

std::expected<Dfs::DirRow, DfsFileStatus> JanusManager::create_item_vector(const std::string &vector_name,
                                                                           const ActorId &template_owner_id,
                                                                           const std::string &bid_template_name) {
    if (vector_name.empty()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    const auto main_actor_id = node->account_controller()->current_profile().main_id();
    auto template_row = this->get_bid_template(template_owner_id, bid_template_name);
    if (!template_row.has_value()) {
        eCritical("Bid template '{}' not found", bid_template_name);
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    auto store_res =
        node->dfs()->store_vector(main_actor_id, main_actor_id, vector_name, template_owner_id, template_row->file_id);

    if (!store_res.has_value()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    return store_res.value();
}

bool JanusManager::create_default_bid_template(const std::string &template_name) {
    auto default_template = Dfs::CollectionTemplate::create(template_name)
                                .value()
                                .add_fields({ Dfs::Field::Integer("amount").not_null(),
                                              Dfs::Field::String("message").not_null() });

    return this->create_bid_template(template_name, default_template);
}
