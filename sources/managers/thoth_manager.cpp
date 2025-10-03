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

#include "managers/thoth_manager.h"

#include "managers/extrachain_node.h"
#include "dfs/dfs_controller.h"

ThothManager::ThothManager(ExtraChainNode* node)
    : node(node) {
    QObject::connect(node->dfs(), &DfsController::waitDownloaded, [](ActorId owner_id, Dfs::DirRow dir_row) {
    });
    QObject::connect(node->dfs(), &DfsController::added, [this, &node](ActorId owner_id, Dfs::DirRow dir_row) {
        // return;
        if (dir_row.actor_id == node->network_id() && dir_row.name == "Thoth") {
            this->owner_id_ = node->network_id();
            this->file_id_  = dir_row.file_id;
            this->read_all();
        }
    });
}

void ThothManager::start() {
    enabled_ = true;
    // TODO: add downloaded file
}

void ThothManager::stop() {
    enabled_  = false;
    owner_id_ = ActorId();
    file_id_.clear();
}

bool ThothManager::create_thoth_template() {
    auto thoth_template =
        Dfs::CollectionTemplate::create("Thoth").value().add_fields({ Dfs::Field::Blob("owner").not_null(),
                                                                      Dfs::Field::Blob("file_id").not_null(),
                                                                      Dfs::Field::Blob("os").not_null(),
                                                                      Dfs::Field::Blob("token").not_null() });

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto template_res    = node->dfs()->store_template(system_actor_id, thoth_template);
    if (!template_res.has_value()) {
        eCritical("Can't create Thoth template, because {}", template_res.error());
        return false;
    }

    return true;
}

bool ThothManager::create_thoth_vector() {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(network_id,
                                                                  Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                  "Thoth");
    if (!search_result.has_value()) {
        return false;
    }

    auto store_res =
        node->dfs()->store_vector(network_id, network_id, "Thoth", network_id, search_result->file_id);
    if (!store_res.has_value()) {
        eCritical("Can't create Thoth database, because {}", store_res.error());
        return false;
    }

    return true;
}
bool ThothManager::read_all() {
    auto file_row = node->dfs()->read_file_status(node->network_id(), "Thoth");
    if (!file_row.has_value()) {
        // TODO: wait for exists
        return false;
    }

    if (file_row->state != Dfs::FileState::Ready) {
        node->dfs()->add_to_waiting_file(owner_id_, file_id_);
        node->dfs()->request_file(owner_id_, file_id_);
        return false;
    }

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto security_key = Dfs::DataSecurityActor { .sender_id = ActorId(), .receiver_id = node->network_id() };
    auto rows         = node->dfs()->read_vector_rows(owner_id_, file_id_, "where status = '1'", security_key);
    if (!rows.has_value()) {
        return false;
    }

    // TODO: rows to infos_

    return true;
}

bool ThothManager::add_thoth_record(const ActorId& owner_id, const std::string& file_id) {
    auto file_row = node->dfs()->read_file_status(node->network_id(), "Thoth");
    if (!file_row.has_value()) {
        // TODO: wait for exists
        return false;
    }

    owner_id_ = node->network_id();
    file_id_  = file_row->file_id;

    if (file_row->state != Dfs::FileState::Ready) {
        node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        node->dfs()->request_file(node->network_id(), file_row->file_id);
        return false;
    }

    // auto main_id   = account_controller_->current_profile().main_id();
    auto system_id = node->account_controller()->system_actor().id();

    // check db file, queue

    auto thoth_data =
        ThothData { .id = "", .timestamp = 0, .actor = system_id, .owner = owner_id, .file_id = file_id };

    // DbRow row          = { { "owner", owner_id.to_string() }, { "file", file_id } };
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };

    auto res = node->dfs()->add_vector_row(node->network_id(), file_id_, thoth_data, system_id, security_key);

    if (!res) {
        return false;
    }

    return res;
}

void ThothManager::dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id) {
    if (!enabled_) {
        return;
    }

    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };
    if (!infos_.contains(file_link)) {
        return;
    }

    // use os and token to send to service
}

void ThothManager::network_thoth_record(const ActorId&     owner_id,
                                        const std::string& file_id,
                                        const std::string& os,
                                        const std::string& token) {
    auto file_link    = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };
    auto thoth_info   = ThothInfo { .os = os, .token = token };
    infos_[file_link] = thoth_info;
}
