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

#include "managers/extrachain_node.h"
#include "dfs/dfs_controller.h"

JanusManager::JanusManager(ExtraChainNode* node)
    : node(node) {
}

bool JanusManager::create_task(JanusTask task) {
    auto main_id   = node->account_controller()->current_profile().main_id();
    auto json      = Json::serialize(task);
    auto json_data = ByteArray(json).toVector();
    auto task_id   = "TaskId";

    auto dfs_result =
        node->dfs()->store_data_as_file(main_id, main_id, json_data, Dfs::Basic::TEMPLATE_JANUS, task_id);

    if (!dfs_result.has_value()) {
        return false;
    }

    return true;
}

bool JanusManager::create_janus_template() {
    auto system_actor_id = node->account_controller()->system_actor().id();

    auto janus_template = Dfs::CollectionTemplate::create("JanusTasks")
                              .value()
                              .use_id()
                              .add_fields({ Dfs::Field::Blob("owner").not_null(),
                                            Dfs::Field::Blob("file_id").not_null(),
                                            Dfs::Field::Integer("state") });

    auto template_res = node->dfs()->store_template(system_actor_id, janus_template);
    if (!template_res.has_value()) {
        eCritical("Can't create Janus template, because {}", template_res.error());
        return false;
    }

    return true;
}

bool JanusManager::create_janus_vector() {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }
    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "Janus");
    if (!search_result.has_value()) {
        return false;
    }
    auto store_res =
        node->dfs()->store_vector(network_id, network_id, "JanusTasks", network_id, search_result->file_id);
    if (!store_res.has_value()) {
        eCritical("Can't create Janus database, because {}", store_res.error());
        return false;
    }
    return true;
}

bool JanusManager::add_janus_task(const ActorId& owner_id, const std::string& file_id) {
    auto file_row = node->dfs()->read_file_status(node->network_id(), "Janus");
    if (!file_row.has_value()) {
        // TODO: wait for exists
        return false;
    }

    // owner_id_ = node->network_id();
    // file_id_  = file_row->file_id;

    if (file_row->state != Dfs::FileState::Ready) {
        node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        node->dfs()->request_file(node->network_id(), file_row->file_id);
        return false;
    }

    auto system_id = node->account_controller()->system_actor().id();

    auto janus_data = JanusData { .id        = Utils::generate_random_hex(6),
                                  .timestamp = 0,
                                  .actor     = system_id,
                                  .owner     = owner_id,
                                  .file_id   = file_id,
                                  .state     = 0 };

    auto res = node->dfs()->add_vector_row(node->network_id(), file_id_, janus_data, system_id);

    return res.has_value();
}
