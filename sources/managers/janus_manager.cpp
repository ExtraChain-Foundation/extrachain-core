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

JanusManager::JanusManager(ExtraChainNode *node)
    : node(node) {
}

// bool JanusManager::create_task(JanusTask task) {
//     auto main_id   = node->account_controller()->current_profile().main_id();
//     auto json      = Json::serialize(task);
//     auto json_data = ByteArray(json).toVector();
//     auto task_id   = "Task_" + Utils::generate_random_hex(6);

//     auto dfs_result =
//         node->dfs()->store_data_as_file(main_id, main_id, json_data, Dfs::Basic::TEMPLATE_JANUS, task_id);

//     if (!dfs_result.has_value()) {
//         return false;
//     }

//     return true;
// }

bool JanusManager::create_argentum_vector() {
    bool res  = node->create_file_id_vector("ArgentumTasks", FileIdState::With);
    bool res2 = node->create_file_id_vector("ArgentumDevices", FileIdState::With);
    eLog("create arg vector: {} {}", res, res2);
    return res && res2;
}

bool JanusManager::create_janus_template() {
    auto janus_template = Dfs::CollectionTemplate::create("JanusBids")
                              .value()
                              .add_fields({ Dfs::Field::Integer("amount").not_null(),
                                            Dfs::Field::String("letter").not_null(),
                                            Dfs::Field::Integer("expected_time").not_null() });

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto template_res    = node->dfs()->store_template(system_actor_id, janus_template);

    if (!template_res.has_value()) {
        eCritical("Can't create janus template, because {}", template_res.error());
        return false;
    }

    return true;
}

std::expected<Dfs::DirRow, DfsFileStatus> JanusManager::create_janus_vector(const std::string &task_name) {
    if (task_name.empty()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    // auto row = node->dfs()->read_file_status_self(task_name);
    // if (row.has_value()) {
    //     return std::unexpected(DfsFileStatus::Existed);
    // }

    const auto main_actor_id = node->account_controller()->current_profile().main_id();
    auto       network_id    = node->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "JanusBids");
    if (!search_result.has_value()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    auto store_chat_res =
        node->dfs()->store_vector(main_actor_id, main_actor_id, task_name, network_id, search_result->file_id);

    if (!store_chat_res.has_value()) {
        return std::unexpected(DfsFileStatus::CantCreate);
    }

    return store_chat_res.value();
}

std::optional<std::string> JanusManager::bid(const ActorId     &vector_owner_id,
                                             const std::string &vector_file_id,
                                             // const ActorId        &actor_id,
                                             // const std::string    &file_id,
                                             const BigNumberFloat &amount,
                                             std::uint32_t         expected_time,
                                             const std::string    &letter) {
    auto file_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(),
                                                                   vector_owner_id,
                                                                   vector_file_id);
    if (!file_row.has_value()) {
        // TODO: wait for exists
        return std::nullopt;
    }

    if (file_row->state != Dfs::FileState::Ready) {
        // node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        // node->dfs()->request_file(node->network_id(), file_row->file_id);
        return std::nullopt;
    }

    auto main_id = node->account_controller()->current_profile().main_id();

    auto janus_bid = JanusBid { // .id            = Utils::generate_random_hex(6),
                                .timestamp     = 0,
                                .actor         = main_id,
                                .amount        = amount.to_string(NumeralBase::Dec),
                                .expected_time = expected_time,
                                .letter        = letter
    };

    auto res = node->dfs()->add_vector_row(vector_owner_id, vector_file_id, janus_bid, main_id);
    if (!res) {
        return std::nullopt;
    }

    return "";
}
