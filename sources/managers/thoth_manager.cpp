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

#include "chat/chat_manager.h"

#include <algorithm>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "managers/extrachain_node.h"
#include "dfs/dfs_controller.h"

namespace {
using ThothRecordKey = std::tuple<std::string, std::string, std::string, std::string>;

ThothRecordKey make_thoth_record_key(const ActorId&     owner_id,
                                     const std::string& file_id,
                                     const std::string& token,
                                     const std::string& custom) {
    return { owner_id.to_string(), file_id, token, custom };
}
}

ThothManager::ThothManager(ExtraChainNode* node, QObject* parent)
    : node(node)
    , QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
    QObject::connect(node->dfs(),
                     &DfsController::waitDownloaded,
                     [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
                         if (owner_id == node->network_id() && dir_row.name == "Thoth" /* && vector */) {
                             this->owner_id_ = node->network_id();
                             this->file_id_  = dir_row.file_id;
                             this->read_all(!enabled_);
                             this->flush_pending_records();
                         }
                     });

    QObject::connect(node->dfs(), &DfsController::downloaded, [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
        if (owner_id == node->network_id() && dir_row.name == "Thoth") {
            this->owner_id_ = node->network_id();
            this->file_id_  = dir_row.file_id;
            this->read_all(!enabled_);
            this->flush_pending_records();
        }
    });

}

void ThothManager::start() {
    enabled_ = true;
    // TODO: add downloaded file
    if (this->read_all(false)) {
        this->flush_pending_records();
    }
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
        node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        node->dfs()->request_file(node->network_id(), file_row->file_id);
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

    if (!is_my) {
        infos_.clear();
    }

    for (const auto& row : *rows) {
        apply_thoth_row(row);
    }

    return true;
}

bool ThothManager::add_thoth_record(const ActorId&     owner_id,
                                    const std::string& file_id,
                                    const std::string& custom) {
    auto add_result = try_add_thoth_record(owner_id, file_id, custom);
    if (add_result == ThothAddResult::Success) {
        return true;
    }

    if (add_result == ThothAddResult::Retry) {
        enqueue_thoth_record(owner_id, file_id, custom);
    }
    return false;
}

void ThothManager::enqueue_thoth_record(const ActorId&     owner_id,
                                        const std::string& file_id,
                                        const std::string& custom) {
    auto it = std::find_if(pending_records_.begin(),
                           pending_records_.end(),
                           [&owner_id, &file_id, &custom](const ThothPendingRecord& record) {
                               return record.owner_id == owner_id && record.file_id == file_id
                                   && record.custom == custom;
                           });
    if (it == pending_records_.end()) {
        pending_records_.push_back({ .owner_id = owner_id, .file_id = file_id, .custom = custom });
    }
}

ThothAddResult ThothManager::try_add_thoth_record(const ActorId&     owner_id,
                                                  const std::string& file_id,
                                                  const std::string& custom,
                                                  bool               check_existing) {
    if (ios_token_.empty()) {
        return ThothAddResult::Retry;
    }

    auto file_row = node->dfs()->read_file_status(node->network_id(), "Thoth");
    if (!file_row.has_value()) {
        // TODO: wait for exists
        return ThothAddResult::Retry;
    }

    owner_id_ = node->network_id();
    file_id_  = file_row->file_id;

    if (file_row->state != Dfs::FileState::Ready) {
        node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        node->dfs()->request_file(node->network_id(), file_row->file_id);
        return ThothAddResult::Retry;
    }

    // auto main_id   = account_controller_->current_profile().main_id();
    auto system_id = node->account_controller()->system_actor().id();

    // check db file, queue
    if (check_existing && thoth_record_exists(owner_id, file_id, custom)) {
        return ThothAddResult::Success;
    }

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
        return ThothAddResult::Failed;
    }

    return ThothAddResult::Success;
}

bool ThothManager::thoth_record_exists(const ActorId&     owner_id,
                                       const std::string& file_id,
                                       const std::string& custom) {
    if (file_id_.empty() || ios_token_.empty()) {
        return false;
    }

    auto system_id    = node->account_controller()->system_actor().id();
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };
    auto rows         = node->dfs()->read_vector_rows(node->network_id(),
                                              file_id_,
                                              fmt::format("where status = '1' AND actor = '{}'", system_id),
                                              security_key);
    if (!rows.has_value()) {
        return false;
    }

    for (const auto& row : *rows) {
        auto owner_it  = row.find("owner");
        auto file_it   = row.find("file_id");
        auto token_it  = row.find("token");
        auto custom_it = row.find("custom");
        if (owner_it == row.end() || file_it == row.end() || token_it == row.end() || custom_it == row.end()) {
            continue;
        }

        if (make_thoth_record_key(ActorId(owner_it->second), file_it->second, token_it->second, custom_it->second)
            == make_thoth_record_key(owner_id, file_id, ios_token_, custom)) {
            return true;
        }
    }

    return false;
}

void ThothManager::flush_pending_records() {
    if (node->account_controller()->empty()) {
        return;
    }

    if (!pending_remove_token_.empty()) {
        auto stale = std::move(pending_remove_token_);
        pending_remove_token_.clear();
        eLog("[Thoth] retry stale-token removal: {}", stale);
        remove_own_records_with_token(stale);
    }

    if (pending_records_.empty()) {
        return;
    }

    auto records = std::move(pending_records_);
    pending_records_.clear();

    std::optional<std::set<ThothRecordKey>> existing_records;
    if (!file_id_.empty() && !ios_token_.empty()) {
        auto system_id    = node->account_controller()->system_actor().id();
        auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };
        auto rows         = node->dfs()->read_vector_rows(node->network_id(),
                                                  file_id_,
                                                  fmt::format("where status = '1' AND actor = '{}'", system_id),
                                                  security_key);
        if (rows.has_value()) {
            existing_records = std::set<ThothRecordKey> {};
            for (const auto& row : *rows) {
                auto owner_it  = row.find("owner");
                auto file_it   = row.find("file_id");
                auto token_it  = row.find("token");
                auto custom_it = row.find("custom");
                if (owner_it == row.end() || file_it == row.end() || token_it == row.end()
                    || custom_it == row.end()) {
                    continue;
                }

                existing_records->insert(
                    make_thoth_record_key(ActorId(owner_it->second), file_it->second, token_it->second, custom_it->second));
            }
        }
    }

    for (const auto& record : records) {
        auto record_key = make_thoth_record_key(record.owner_id, record.file_id, ios_token_, record.custom);
        if (existing_records.has_value() && existing_records->contains(record_key)) {
            continue;
        }

        auto result = try_add_thoth_record(record.owner_id,
                                           record.file_id,
                                           record.custom,
                                           !existing_records.has_value());
        if (result == ThothAddResult::Success && existing_records.has_value()) {
            existing_records->insert(record_key);
        } else if (result == ThothAddResult::Retry) {
            enqueue_thoth_record(record.owner_id, record.file_id, record.custom);
        }
    }
}

void ThothManager::dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id, const DbRow& row) {
    if (!enabled_) {
        return;
    }

    if (node->network_id() == owner_id_ && file_id == file_id_) {
        this->network_thoth_record(owner_id_, file_id_, row);
        return;
    }

    auto status_it = row.find("status");
    if (status_it == row.end() || status_it->second != "1") {
        return;
    }

    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };
    if (!infos_.contains(file_link)) {
        return;
    }

    std::set<std::string> sent_tokens;
    for (const auto& el : std::as_const(infos_[file_link])) {
        auto actor_id = ActorId(row.at("actor"));
        if (el.ignored.contains(actor_id)) {
            continue;
        }
        if (!sent_tokens.insert(el.token).second) {
            continue;
        }

        auto username = this->read_username(actor_id);
        this->send_to_service(el, username);
    }
}

void ThothManager::network_thoth_record(const ActorId& owner_id, const std::string& file_id, const DbRow& row) {
    auto id_it = row.find("id");
    if (id_it == row.end()) {
        return;
    }

    auto status_it = row.find("status");
    if (status_it != row.end() && status_it->second == "0") {
        remove_thoth_info(id_it->second);
        return;
    }

    auto security_key = Dfs::DataSecurityActor { .sender_id = ActorId(), .receiver_id = node->network_id() };
    auto rows         = node->dfs()->read_vector_rows(owner_id_,
                                              file_id_,
                                              fmt::format("where status = '1' AND id = '{}'", id_it->second),
                                              security_key);
    if (!rows.has_value()) {
        return;
    }

    if (rows->empty()) {
        remove_thoth_info(id_it->second);
        return;
    }

    apply_thoth_row(rows->at(0));
}

void ThothManager::apply_thoth_row(const DbRow& row) {
    auto id_it = row.find("id");
    if (id_it == row.end()) {
        return;
    }

    remove_thoth_info(id_it->second);

    auto file_link  = Dfs::FileLink { .owner_id = ActorId(row.at("owner")), .file_id = row.at("file_id") };
    auto custom     = Json::deserialize<ThothCustom>(row.at("custom"));
    auto thoth_info = ThothInfo { .id      = id_it->second,
                                  .os      = row.at("os"),
                                  .token   = row.at("token"),
                                  .ignored = custom.has_value() ? custom->ignored : std::set<ActorId>({}) };
    infos_[file_link].insert(thoth_info);
}

void ThothManager::remove_thoth_info(const std::string& id) {
    for (auto info_it = infos_.begin(); info_it != infos_.end();) {
        for (auto thoth_it = info_it->second.begin(); thoth_it != info_it->second.end();) {
            if (thoth_it->id == id) {
                thoth_it = info_it->second.erase(thoth_it);
            } else {
                ++thoth_it;
            }
        }

        if (info_it->second.empty()) {
            info_it = infos_.erase(info_it);
        } else {
            ++info_it;
        }
    }
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
    eLog("Thoth local push POST {} token={} body={}", url.toString().toStdString(), info.token, service_message.body);
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

void ThothManager::set_device_token(const std::string& token) {
    if (token.empty()) {
        return;
    }

    if (ios_token_.empty()) {
        // Fresh process: recover the previously registered token from disk so a token
        // change across restarts still removes the stale rows.
        auto persisted = load_persisted_device_token();
        if (!persisted.empty() && persisted != token) {
            ios_token_ = persisted;
        }
    }

    // Same token as before: nothing changed, avoid re-registering.
    if (token == ios_token_) {
        persist_device_token(token);
        flush_pending_records();
        return;
    }

    // Token changed: drop this device's rows carrying the OLD token so it stops
    // getting pushes, then register the new token for all my chats below.
    std::string old_token = ios_token_;
    ios_token_            = token;
    persist_device_token(token);

    if (!old_token.empty()) {
        remove_own_records_with_token(old_token);
    }

    // The per-chat re-registration is driven by ChatManager::read_chats(), which calls
    // reconcile_tokens_for_chats() once the chat list is actually ready. Here we just
    // reset the guard so the next read_chats() re-registers under the new token.
    reconciled_token_.clear();
    reconciled_chats_count_ = 0;

    flush_pending_records();
}

// The node's working directory is the data dir (see prepare_folders), so a relative
// path lands next to the profile data.
void ThothManager::persist_device_token(const std::string& token) {
    QFile file(".thoth_device_token");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(token.data(), qint64(token.size()));
    }
}

std::string ThothManager::load_persisted_device_token() {
    QFile file(".thoth_device_token");
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll().toStdString();
}

// Registers the current token for every chat in the given (already-ready) list, called
// by ChatManager::read_chats(). The per-chat walk lives in the chat branch (needs
// per-chat identity); the base only retries deferred work at this ready point.
void ThothManager::reconcile_tokens_for_chats(const std::vector<Chat::Chat>& chats) {
    (void) chats;
    if (ios_token_.empty()) {
        return;
    }

    flush_pending_records();
}

void ThothManager::remove_own_records_with_token(const std::string& token) {
    if (token.empty()) {
        return;
    }

    // Called as early as nodeInitialised: no profile yet -> system_actor() would abort.
    if (node->account_controller()->empty()) {
        pending_remove_token_ = token;
        return;
    }

    if (file_id_.empty()) {
        auto file_row = node->dfs()->read_file_status(node->network_id(), "Thoth");
        if (!file_row.has_value() || file_row->state != Dfs::FileState::Ready) {
            // Thoth vector not ready yet: retry on the next flush_pending_records().
            pending_remove_token_ = token;
            return;
        }
        owner_id_ = node->network_id();
        file_id_  = file_row->file_id;
    }

    auto system_id    = node->account_controller()->system_actor().id();
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };
    auto rows         = node->dfs()->read_vector_rows(node->network_id(),
                                              file_id_,
                                              fmt::format("where status = '1' AND actor = '{}'", system_id),
                                              security_key);
    if (!rows.has_value()) {
        pending_remove_token_ = token;
        return;
    }

    for (const auto& row : *rows) {
        auto id_it    = row.find("id");
        auto token_it = row.find("token");
        if (id_it == row.end() || token_it == row.end()) {
            continue;
        }
        if (token_it->second != token) {
            continue;
        }

        eLog("[Thoth] remove stale token row id={} token={}", id_it->second, token);
        node->dfs()->remove_vector_row(node->network_id(), file_id_, id_it->second, system_id);
    }
}

void ThothManager::set_ios_token(const std::string& token) {
    set_device_token(token);
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
