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

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "managers/extrachain_node.h"
#include "dfs/dfs_controller.h"

ThothManager::ThothManager(ExtraChainNode* node, QObject* parent)
    : node(node)
    , QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
    QObject::connect(node->dfs(),
                     &DfsController::waitDownloaded,
                     [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
                         if (owner_id == node->network_id() && dir_row.name == "Thoth" /* && vector */) {
                             this->read_all(!enabled_);
                         }
                     });

    QObject::connect(node->dfs(), &DfsController::downloaded, [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
        if (owner_id == node->network_id() && dir_row.name == "Thoth") {
            this->owner_id_ = node->network_id();
            this->file_id_  = dir_row.file_id;
            if (enabled_) {
                this->read_all(!enabled_);
            }
        }
    });
}

void ThothManager::start() {
    enabled_ = true;
    // TODO: add downloaded file
    this->read_all(false);
}

void ThothManager::stop() {
    enabled_  = false;
    owner_id_ = ActorId();
    file_id_.clear();
}

bool ThothManager::create_thoth_template() {
    auto thoth_template = Dfs::CollectionTemplate::create("Thoth").value().use_id().add_fields(
        { Dfs::Field::Blob("owner").not_null(),
          Dfs::Field::Blob("file_id").not_null(),
          Dfs::Field::Blob("os").not_null(),
          Dfs::Field::Blob("token").not_null(),
          Dfs::Field::Blob("custom") });

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
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
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

// TODO: read for user

bool ThothManager::read_all(bool is_my) {
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

    owner_id_ = node->network_id();
    file_id_  = file_row->file_id;

    auto security_key =
        Dfs::DataSecurityActor { .sender_id = is_my ? node->account_controller()->system_actor().id() : ActorId(),
                                 .receiver_id = node->network_id() };

    auto where =
        is_my ? fmt::format("where status = '1' AND actor = '{}'", node->account_controller()->system_actor().id())
              : "where status = '1'";
    auto rows = node->dfs()->read_vector_rows(owner_id_, file_id_, where, security_key);
    if (!rows.has_value()) {
        return false;
    }

    for (const auto& row : *rows) {
        auto file_link  = Dfs::FileLink { .owner_id = ActorId(row.at("owner")), .file_id = row.at("file_id") };
        auto custom     = Json::deserialize<ThothCustom>(row.at("custom"));
        auto thoth_info = ThothInfo { .os      = row.at("os"),
                                      .token   = row.at("token"),
                                      .ignored = custom.has_value() ? custom->ignored : std::set<ActorId>({}) };
        // eLog("Loaded --------- : {}", thoth_info);
        // eLog("Loaded --------- : {}", file_link);
        infos_[file_link].insert(thoth_info);
    }

    return true;
}

bool ThothManager::add_thoth_record(const ActorId&     owner_id,
                                    const std::string& file_id,
                                    const std::string& custom) {
    if (ios_token_.empty()) {
        return false;
    }

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

    auto thoth_data = ThothData { .id        = Utils::generate_random_hex(6),
                                  .timestamp = 0,
                                  .actor     = system_id,
                                  .owner     = owner_id,
                                  .file_id   = file_id,
#ifdef Q_OS_IOS
                                  .os = "iOS",
#endif
#ifdef Q_OS_ANDROID
                                  .os = "Android",
#endif
                                  .token  = ios_token_,
                                  .custom = custom };

    // DbRow row          = { { "owner", owner_id.to_string() }, { "file", file_id } };
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };
    auto res = node->dfs()->add_vector_row(node->network_id(), file_id_, thoth_data, system_id, security_key);

    if (!res) {
        return false;
    }

    return res;
}

void ThothManager::dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id, const DbRow& row) {
    if (!enabled_) {
        return;
    }

    if (node->network_id() == owner_id_ && file_id == file_id_) {
        this->network_thoth_record(owner_id_, file_id_, row);
    }

    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };
    if (!infos_.contains(file_link)) {
        return;
    }

    for (const auto& el : std::as_const(infos_[file_link])) {
        auto actor_id = ActorId(row.at("actor"));
        if (el.ignored.contains(actor_id)) {
            continue;
        }

        auto username = this->read_username(actor_id);
        this->send_to_service(el, username);
    }
}

void ThothManager::network_thoth_record(const ActorId& owner_id, const std::string& file_id, const DbRow& row) {

    auto security_key = Dfs::DataSecurityActor { .sender_id = ActorId(), .receiver_id = node->network_id() };
    auto rows         = node->dfs()->read_vector_rows(owner_id_,
                                              file_id_,
                                              fmt::format("where status = '1' AND id = '{}'", row.at("id")),
                                              security_key);
    if (!rows.has_value()) {
        return;
    }

    if (rows->empty()) {
        return;
    }

    auto file_link =
        Dfs::FileLink { .owner_id = ActorId(rows->at(0).at("owner")), .file_id = rows->at(0).at("file_id") };
    auto custom     = Json::deserialize<ThothCustom>(rows->at(0).at("custom"));
    auto thoth_info = ThothInfo { .os      = rows->at(0).at("os"),
                                  .token   = rows->at(0).at("token"),
                                  .ignored = custom.has_value() ? custom->ignored : std::set<ActorId>({}) };
    infos_[file_link].insert(thoth_info);
}

bool ThothManager::send_to_service(const ThothInfo& info, const std::string& username) {
    QUrl            url("http://localhost:5425/send");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto service_message =
        ThothServiceMessage { .device_token = info.token,
                              .title        = "Messenger",
                              .body         = username.empty() ? "Raccoon brings word from the shadows"
                                                               : fmt::format("Message from @{}", username) };

    QByteArray     data  = QByteArray::fromStdString(Json::serialize(service_message));
    QNetworkReply* reply = m_networkManager->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();

            emit sendSuccess(QString::fromUtf8(response));
        } else {
            emit sendFailed(reply->errorString());
        }
        reply->deleteLater();
    });

    return true;
}

void ThothManager::set_ios_token(const std::string& token) {
    ios_token_ = token;
}

std::string ThothManager::read_username(const ActorId& actor_id) {
    if (actor_id.is_zero()) {
        return "";
    }

    if (usernames_file_id_.empty()) {
        auto search_result =
            Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                              node->network_id(),
                                                                              Dfs::Basic::TEMPLATE_VECTOR,
                                                                              "Usernames");

        if (!search_result.has_value()) {
            return "";
        }

        if (search_result->state == Dfs::FileState::Ready) {
            this->usernames_file_id_ = search_result->file_id;
        } else {
            return "";
        }
    }

    auto row = node->dfs()->read_vector_row(node->network_id(), usernames_file_id_, actor_id.to_string());
    if (!row.has_value()) {
        return "";
    }

    if (row->find("name") == row->end()) {
        return "";
    }

    return row->at("name").c_str();
}
