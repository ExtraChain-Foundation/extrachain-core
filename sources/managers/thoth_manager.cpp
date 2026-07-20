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
#include <string_view>
#include <utility>

#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStringList>
#include <QSysInfo>

#include "managers/extrachain_node.h"
#include "dfs/dfs_controller.h"

namespace {
constexpr std::string_view THOTH_DATABASE = "ThothDevicesV2";
}

ThothManager::ThothManager(ExtraChainNode* node, QObject* parent)
    : node(node)
    , QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
    QObject::connect(node->dfs(),
                     &DfsController::waitDownloaded,
                     [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
                         if (owner_id != node->network_id()) {
                             return;
                         }
                         if (dir_row.name == THOTH_DATABASE) {
                             this->owner_id_ = node->network_id();
                             this->file_id_  = dir_row.file_id;
                             this->read_all(!enabled_);
                             this->flush_pending_records();
                             this->verify_self_registration();
                         } else if (dir_row.name == "Thoth") {
                             this->legacy_file_id_ = dir_row.file_id;
                             this->legacy_read_all(!enabled_);
                         }
                     });

    QObject::connect(node->dfs(), &DfsController::downloaded, [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
        if (owner_id != node->network_id()) {
            return;
        }
        if (dir_row.name == THOTH_DATABASE) {
            this->owner_id_ = node->network_id();
            this->file_id_  = dir_row.file_id;
            this->read_all(!enabled_);
            this->flush_pending_records();
            this->verify_self_registration();
        } else if (dir_row.name == "Thoth") {
            this->legacy_file_id_ = dir_row.file_id;
            this->legacy_read_all(!enabled_);
        }
    });

}

void ThothManager::start() {
    enabled_ = true;
    // TODO: add downloaded file
    if (this->read_all(false)) {
        this->flush_pending_records();
    }
    this->legacy_read_all(false);
}

void ThothManager::stop() {
    enabled_  = false;
    owner_id_ = ActorId();
    file_id_.clear();
}

bool ThothManager::create_thoth_dictionary() {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    if (node->dfs()->read_file_status(network_id, std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY).has_value()) {
        return true;
    }

    auto store_res = node->dfs()->store_dictionary(network_id, network_id, std::string(THOTH_DATABASE));
    if (!store_res.has_value()) {
        eCritical("Can't create Thoth device registry, because {}", store_res.error());
        return false;
    }

    return true;
}

// TODO: read for user

bool ThothManager::read_all(bool is_my) {
    auto file_row = node->dfs()->read_file_status(node->network_id(), std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY);
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
    auto rows = node->dfs()->read_vector_rows(owner_id_, file_id_, where, security_key, Dfs::FileType::Dictionary);
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

// One dictionary key per account: opaque, carries no plaintext owner/file_id/token.
std::string ThothManager::registry_key() {
    if (!registry_key_.empty()) {
        return registry_key_;
    }
    if (node->account_controller()->empty()) {
        return {};
    }
    auto actor = node->account_controller()->derive_local_actor("Thoth", ActorType::User);
    if (actor.id().is_zero()) {
        return {};
    }
    registry_key_ = actor.id().to_string().substr(0, 20);
    return registry_key_;
}

bool ThothManager::add_thoth_record(const ActorId&     owner_id,
                                    const std::string& file_id,
                                    const std::string& custom) {
    enqueue_thoth_record(owner_id, file_id, custom);
    flush_pending_records();
    return true;
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

// The single registry writer: read the whole merged JSON, merge this device's records,
// write it back. Never erases sibling devices or other chats. Keeps queue/flags on bail.
void ThothManager::flush_pending_records() {
    if (node->account_controller()->empty() || ios_token_.empty() || registry_key().empty()) {
        return;
    }

    if (pending_records_.empty() && !force_registry_write_) {
        return;
    }

    auto file_row = node->dfs()->read_file_status(node->network_id(), std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY);
    if (!file_row.has_value()) {
        return; // registry file absent locally: retry later
    }
    if (file_row->state != Dfs::FileState::Ready) {
        node->dfs()->add_to_waiting_file(node->network_id(), file_row->file_id);
        node->dfs()->request_file(node->network_id(), file_row->file_id);
        return;
    }

    owner_id_ = node->network_id();
    file_id_  = file_row->file_id;

    load_or_create_device_id();

    auto system_id    = node->account_controller()->system_actor().id();
    // Same system actor for all of an account's devices: decrypts values written by siblings.
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };

    ThothRegistry reg;
    auto row = node->dfs()->read_vector_row(node->network_id(),
                                            file_id_,
                                            registry_key(),
                                            security_key,
                                            Dfs::FileType::Dictionary);
    if (row.has_value()) {
        auto value_it = row->find("value");
        if (value_it != row->end()) {
            auto parsed = Json::deserialize<ThothRegistry>(value_it->second);
            if (parsed.has_value()) {
                reg = std::move(parsed.value());
            } else {
                eWarning("[Thoth] registry parse failed, starting from empty registry");
            }
        }
    }

    const std::string os = detect_os();

    bool changed = false;
    for (const auto& record : pending_records_) {
        auto  chat_key = fmt::format("{}:{}", record.owner_id, record.file_id);
        auto& chat     = reg.chats[chat_key];
        chat.owner     = record.owner_id;
        chat.file_id   = record.file_id;

        auto  name       = effective_device_name();
        auto  new_record = ThothDeviceRecord { .os = os, .token = ios_token_, .custom = record.custom, .name = name };
        auto  dev_it     = chat.devices.find(device_id_);
        // updated_at is intentionally excluded from the change test: it must not, by itself,
        // force a rewrite. But whenever we do write, we refresh it.
        if (dev_it == chat.devices.end() || dev_it->second.os != new_record.os
            || dev_it->second.token != new_record.token || dev_it->second.custom != new_record.custom
            || dev_it->second.name != new_record.name) {
            new_record.updated_at    = std::uint64_t(QDateTime::currentMSecsSinceEpoch());
            chat.devices[device_id_] = new_record;
            changed                  = true;
        }
    }

    // Refresh this device's entry in every chat it already occupies: a token/name
    // rotation must propagate even when nothing new is pending.
    for (auto& [chat_key, chat] : reg.chats) {
        auto dev_it = chat.devices.find(device_id_);
        if (dev_it == chat.devices.end()) {
            continue;
        }
        auto name = effective_device_name();
        if (dev_it->second.os != os || dev_it->second.token != ios_token_ || dev_it->second.name != name) {
            dev_it->second.os         = os;
            dev_it->second.token      = ios_token_;
            dev_it->second.name       = name;
            dev_it->second.updated_at = std::uint64_t(QDateTime::currentMSecsSinceEpoch());
            changed                   = true;
        }
    }

    if (!changed && !force_registry_write_) {
        pending_records_.clear();
        return;
    }

    auto res = node->dfs()->dictionary_set_value(node->network_id(),
                                                 file_id_,
                                                 registry_key(),
                                                 Json::serialize(reg),
                                                 system_id,
                                                 security_key);
    if (!res) {
        return; // keep pending, retry later
    }

    // Keep the startup force flag until a write that carried real chat records:
    // early flushes from DFS callbacks can be lost to the file-sync race, and the
    // first reconcile (chats ready) must still be able to force its self-heal write.
    if (!pending_records_.empty()) {
        force_registry_write_ = false;
    }
    pending_records_.clear();
}

void ThothManager::dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id, const DbRow& row) {
    if (!enabled_) {
        return;
    }

    if (node->network_id() == owner_id_ && file_id == file_id_) {
        this->network_thoth_record(owner_id_, file_id_, row);
        return;
    }

    // Legacy "Thoth" vector realtime record (read-only compat).
    if (node->network_id() == owner_id && !legacy_file_id_.empty() && file_id == legacy_file_id_) {
        this->legacy_network_thoth_record(node->network_id(), legacy_file_id_, row);
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

    auto actor_it = row.find("actor");
    if (actor_it == row.end()) {
        return;
    }
    auto security_key = Dfs::DataSecurityActor { .sender_id = ActorId(actor_it->second),
                                                  .receiver_id = node->network_id() };
    auto current = node->dfs()->read_vector_row(owner_id, file_id, id_it->second, security_key,
                                                 Dfs::FileType::Dictionary);
    if (!current.has_value()) {
        return;
    }
    apply_thoth_row(current.value());
}

void ThothManager::apply_thoth_row(const DbRow& row) {
    auto id_it = row.find("id");
    if (id_it == row.end()) {
        return;
    }

    // id is the opaque registry key: drop every ThothInfo previously built from it.
    remove_thoth_info(id_it->second);

    auto value_it = row.find("value");
    if (value_it == row.end()) {
        return;
    }
    auto registry = Json::deserialize<ThothRegistry>(value_it->second);
    if (!registry.has_value()) {
        return;
    }

    for (const auto& [chat_key, chat] : registry->chats) {
        auto file_link = Dfs::FileLink { .owner_id = chat.owner, .file_id = chat.file_id };
        for (const auto& [device_id, record] : chat.devices) {
            auto custom     = Json::deserialize<ThothCustom>(record.custom);
            auto thoth_info = ThothInfo { .id      = id_it->second,
                                          .os      = record.os,
                                          .token   = record.token,
                                          .ignored = custom.has_value() ? custom->ignored : std::set<ActorId>({}) };
            infos_[file_link].insert(thoth_info);
        }
    }
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
        // Fresh process: recover this device's token history from disk.
        load_persisted_device_tokens();
    }
    load_or_create_device_id();

    // Same token as before: nothing changed, avoid re-registering.
    if (token == ios_token_) {
        persist_device_tokens();
        flush_pending_records();
        return;
    }

    ios_token_ = token;
    persist_device_tokens();

    // Reset the guard so the next read_chats() re-registers under the new token.
    reconciled_token_.clear();
    reconciled_chats_count_ = 0;
    force_registry_write_   = true;

    flush_pending_records();
}

void ThothManager::set_device_id(const std::string& id) {
    if (id.empty() || id == device_id_) {
        return;
    }
    device_id_            = id;
    // Rewrite the registry under the new device id even if nothing else changed.
    force_registry_write_ = true;
}

void ThothManager::set_device_name(const std::string& name) {
    if (name.empty() || name == device_name_) {
        return;
    }
    device_name_          = name;
    // Rewrite so the new name lands in the registry even if nothing else changed.
    force_registry_write_ = true;
    // Live rename: re-enqueue the reconciled records so the new name is written
    // immediately, not only on the next read_chats().
    for (const auto& record : last_reconciled_records_) {
        enqueue_thoth_record(record.owner_id, record.file_id, record.custom);
    }
    if (!last_reconciled_records_.empty()) {
        flush_pending_records();
    }
}

std::string ThothManager::detect_os() {
#if defined(Q_OS_IOS)
    return "iOS";
#elif defined(Q_OS_ANDROID)
    return "Android";
#elif defined(Q_OS_MACOS)
    return "macOS";
#elif defined(Q_OS_WIN)
    return "Windows";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return QSysInfo::productType().toStdString();
#endif
}

namespace {
// Hardware model from DMI/SMBIOS ("Lenovo ThinkPad X1 Carbon"); empty when
// the vendor left placeholder junk (custom-built PCs) or the platform has none.
QString desktop_hardware_model() {
#if defined(Q_OS_WIN)
    QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
                   QSettings::NativeFormat);
    QString vendor  = bios.value(QStringLiteral("SystemManufacturer")).toString().trimmed();
    QString product = bios.value(QStringLiteral("SystemProductName")).toString().trimmed();
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    auto read_dmi = [](const char* name) {
        QFile f(QStringLiteral("/sys/class/dmi/id/") + QLatin1String(name));
        if (!f.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(f.readAll()).trimmed();
    };
    QString vendor  = read_dmi("sys_vendor");
    QString product = read_dmi("product_name");
    // Lenovo puts the machine-type code in product_name; the human name lives in product_version.
    if (vendor.compare(QStringLiteral("LENOVO"), Qt::CaseInsensitive) == 0) {
        auto version = read_dmi("product_version");
        if (!version.isEmpty()) {
            product = version;
        }
    }
#else
    QString vendor, product;
#endif
    static const QStringList junk = { QStringLiteral("System Product Name"),
                                      QStringLiteral("To Be Filled By O.E.M."),
                                      QStringLiteral("Default string"),
                                      QStringLiteral("System manufacturer"),
                                      QStringLiteral("INVALID") };
    if (product.isEmpty() || junk.contains(product, Qt::CaseInsensitive)) {
        return {};
    }
    if (vendor.isEmpty() || junk.contains(vendor, Qt::CaseInsensitive)) {
        return product;
    }
    // Normalize shouty/legal vendor spellings.
    if (vendor.compare(QStringLiteral("LENOVO"), Qt::CaseInsensitive) == 0) {
        vendor = QStringLiteral("Lenovo");
    } else if (vendor.startsWith(QStringLiteral("Dell"), Qt::CaseInsensitive)) {
        vendor = QStringLiteral("Dell");
    } else if (vendor.startsWith(QStringLiteral("Hewlett"), Qt::CaseInsensitive)
               || vendor.compare(QStringLiteral("HP"), Qt::CaseInsensitive) == 0) {
        vendor = QStringLiteral("HP");
    } else if (vendor.startsWith(QStringLiteral("ASUS"), Qt::CaseInsensitive)) {
        vendor = QStringLiteral("ASUS");
    }
    if (product.startsWith(vendor, Qt::CaseInsensitive)) {
        return product;
    }
    return vendor + " " + product;
}
} // namespace

std::string ThothManager::effective_device_name() {
    if (!device_name_.empty()) {
        return device_name_;
    }
    // Windows/Linux: hardware model from DMI ("Lenovo ThinkPad X1 Carbon").
    auto model = desktop_hardware_model();
    if (!model.isEmpty()) {
        device_name_ = model.toStdString();
        return device_name_;
    }
    // Host name identifies the concrete machine ("MacBook-Pro-Alex"), unlike
    // prettyProductName which is just the OS version and repeats across devices.
    // Mobile hostnames are meaningless ("localhost") — the app supplies the
    // model via set_device_name there; until it does, fall back to the OS name.
    auto host = QSysInfo::machineHostName();
    if (host.endsWith(QStringLiteral(".local"))) {
        host.chop(6);
    }
    if (!host.isEmpty() && host != QStringLiteral("localhost")) {
        device_name_ = host.toStdString();
    } else {
        device_name_ = QSysInfo::prettyProductName().toStdString();
    }
    return device_name_;
}

const std::string& ThothManager::device_id() {
    load_or_create_device_id();
    return device_id_;
}

// Reads this account's own registry (system-actor sender) and aggregates by device id.
std::vector<ThothDeviceInfo> ThothManager::my_devices() {
    std::vector<ThothDeviceInfo> result;

    if (node->account_controller()->empty() || registry_key().empty()) {
        return result;
    }

    auto file_row = node->dfs()->read_file_status(node->network_id(), std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY);
    if (!file_row.has_value() || file_row->state != Dfs::FileState::Ready) {
        return result;
    }

    load_or_create_device_id();

    auto system_id    = node->account_controller()->system_actor().id();
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };

    auto row = node->dfs()->read_vector_row(node->network_id(),
                                            file_row->file_id,
                                            registry_key(),
                                            security_key,
                                            Dfs::FileType::Dictionary);
    if (!row.has_value()) {
        return result;
    }
    auto value_it = row->find("value");
    if (value_it == row->end()) {
        return result;
    }
    auto parsed = Json::deserialize<ThothRegistry>(value_it->second);
    if (!parsed.has_value()) {
        return result;
    }

    // deviceId -> aggregated info (newest record wins for name/os).
    std::map<std::string, ThothDeviceInfo> agg;
    for (const auto& [chat_key, chat] : parsed->chats) {
        for (const auto& [dev_id, record] : chat.devices) {
            auto& info = agg[dev_id];
            info.device_id = dev_id;
            info.chats += 1;
            if (record.updated_at >= info.updated_at) {
                info.updated_at = record.updated_at;
                info.name       = record.name;
                info.os         = record.os;
            }
            info.is_current = (dev_id == device_id_);
        }
    }

    result.reserve(agg.size());
    for (auto& [dev_id, info] : agg) {
        result.push_back(std::move(info));
    }
    return result;
}

// Read->merge->write erasing exactly one device id from every chat. Chats left with no
// devices are dropped. Other device ids are never touched.
bool ThothManager::remove_device(const std::string& device_id) {
    if (device_id.empty() || node->account_controller()->empty() || registry_key().empty()) {
        return false;
    }

    auto file_row = node->dfs()->read_file_status(node->network_id(), std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY);
    if (!file_row.has_value() || file_row->state != Dfs::FileState::Ready) {
        return false;
    }

    load_or_create_device_id();

    auto system_id    = node->account_controller()->system_actor().id();
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };

    auto row = node->dfs()->read_vector_row(node->network_id(),
                                            file_row->file_id,
                                            registry_key(),
                                            security_key,
                                            Dfs::FileType::Dictionary);
    if (!row.has_value()) {
        return false;
    }
    auto value_it = row->find("value");
    if (value_it == row->end()) {
        return false;
    }
    auto parsed = Json::deserialize<ThothRegistry>(value_it->second);
    if (!parsed.has_value()) {
        return false;
    }
    ThothRegistry reg = std::move(parsed.value());

    bool erased_any = false;
    for (auto chat_it = reg.chats.begin(); chat_it != reg.chats.end();) {
        auto dev_it = chat_it->second.devices.find(device_id);
        if (dev_it != chat_it->second.devices.end()) {
            chat_it->second.devices.erase(dev_it);
            erased_any = true;
        }
        if (chat_it->second.devices.empty()) {
            chat_it = reg.chats.erase(chat_it);
        } else {
            ++chat_it;
        }
    }

    if (!erased_any) {
        return true; // nothing to remove: registry already lacks this device
    }

    // Removing the current device: drop any state that would re-register it on the next flush.
    // (Clear before/independently of the write so a queued flush after logout stays a no-op.)
    if (device_id == device_id_) {
        force_registry_write_ = false;
        pending_records_.clear();
    }

    auto res = node->dfs()->dictionary_set_value(node->network_id(),
                                                 file_row->file_id,
                                                 registry_key(),
                                                 Json::serialize(reg),
                                                 system_id,
                                                 security_key);
    return bool(res);
}

// Cwd is the data dir; line 1 = current token, rest = this device's retired tokens.
void ThothManager::persist_device_tokens() {
    QFile file(".thoth_device_token");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(ios_token_.data(), qint64(ios_token_.size()));
}

void ThothManager::load_persisted_device_tokens() {
    QFile file(".thoth_device_token");
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    auto token = QString::fromUtf8(file.readAll()).trimmed();
    if (token.isEmpty()) {
        return;
    }
    ios_token_ = token.toStdString();
}

void ThothManager::load_or_create_device_id() {
    if (!device_id_.empty()) return;
    QFile file(".thoth_device_id");
    if (file.open(QIODevice::ReadOnly)) device_id_ = QString::fromUtf8(file.readAll()).trimmed().toStdString();
    if (!device_id_.empty()) return;
    device_id_ = Utils::generate_random_hex(10);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(device_id_.data(), qint64(device_id_.size()));
}

void ThothManager::reconcile_tokens_for_chats(const std::vector<Chat::Chat>& chats) {
    (void) chats;
    if (ios_token_.empty()) {
        return;
    }
    flush_pending_records();
}

// A freshly synced registry file may be a stale full-file copy that lost this device's
// records (send_broadcast is not guaranteed; file-level sync is last-writer-wins).
// Re-enqueue and rewrite whenever our reconciled records are missing.
void ThothManager::verify_self_registration() {
    if (ios_token_.empty() || last_reconciled_records_.empty()) {
        return;
    }
    if (node->account_controller()->empty() || registry_key().empty()) {
        return;
    }
    auto file_row = node->dfs()->read_file_status(node->network_id(), std::string(THOTH_DATABASE), Dfs::Basic::TEMPLATE_DICTIONARY);
    if (!file_row.has_value() || file_row->state != Dfs::FileState::Ready) {
        return;
    }
    load_or_create_device_id();
    auto system_id    = node->account_controller()->system_actor().id();
    auto security_key = Dfs::DataSecurityActor { .sender_id = system_id, .receiver_id = node->network_id() };
    auto row = node->dfs()->read_vector_row(node->network_id(), file_row->file_id, registry_key(),
                                            security_key, Dfs::FileType::Dictionary);
    ThothRegistry reg;
    if (row.has_value()) {
        auto value_it = row->find("value");
        if (value_it != row->end()) {
            auto parsed = Json::deserialize<ThothRegistry>(value_it->second);
            if (parsed.has_value()) {
                reg = std::move(parsed.value());
            }
        }
    }
    bool missing = false;
    for (const auto& record : last_reconciled_records_) {
        auto chat_it = reg.chats.find(fmt::format("{}:{}", record.owner_id, record.file_id));
        auto ok = chat_it != reg.chats.end() && chat_it->second.devices.contains(device_id_)
               && chat_it->second.devices.at(device_id_).token == ios_token_
               && chat_it->second.devices.at(device_id_).name == effective_device_name();
        if (!ok) {
            enqueue_thoth_record(record.owner_id, record.file_id, record.custom);
            missing = true;
        }
    }
    if (missing) {
        // Rate-limit: a stale-copy ping-pong with file-level sync must not spin.
        auto now = std::uint64_t(QDateTime::currentMSecsSinceEpoch());
        if (now - last_self_heal_ms_ < 15000) {
            return;
        }
        last_self_heal_ms_ = now;
        eLog("[Thoth] self registration lost after sync, rewriting");
        force_registry_write_ = true;
        flush_pending_records();
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

// ---------------------------------------------------------------------------
// Legacy "Thoth" vector compatibility (read-only). Remove this whole section
// once all clients migrated to ThothDevicesV2.
// ---------------------------------------------------------------------------

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

// Read the legacy "Thoth" vector into infos_ so already-registered old clients keep pushes.
bool ThothManager::legacy_read_all(bool is_my) {
    auto file_row = node->dfs()->read_file_status(node->network_id(), "Thoth");
    if (!file_row.has_value()) {
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

    legacy_file_id_ = file_row->file_id;

    auto security_key =
        Dfs::DataSecurityActor { .sender_id = is_my ? node->account_controller()->system_actor().id() : ActorId(),
                                 .receiver_id = node->network_id() };

    auto where =
        is_my ? fmt::format("where status = '1' AND actor = '{}'", node->account_controller()->system_actor().id())
              : "where status = '1'";
    auto rows = node->dfs()->read_vector_rows(node->network_id(), legacy_file_id_, where, security_key);
    if (!rows.has_value()) {
        return false;
    }

    for (const auto& row : *rows) {
        legacy_apply_thoth_row(row);
    }

    return true;
}

// Legacy row layout: fields owner/file_id/os/token/custom read directly (id = row id).
void ThothManager::legacy_apply_thoth_row(const DbRow& row) {
    auto id_it     = row.find("id");
    auto owner_it  = row.find("owner");
    auto file_it   = row.find("file_id");
    auto os_it     = row.find("os");
    auto token_it  = row.find("token");
    auto custom_it = row.find("custom");
    if (id_it == row.end() || owner_it == row.end() || file_it == row.end() || token_it == row.end()) {
        return;
    }

    remove_thoth_info(id_it->second);

    auto file_link = Dfs::FileLink { .owner_id = ActorId(owner_it->second), .file_id = file_it->second };
    std::set<ActorId> ignored;
    if (custom_it != row.end()) {
        auto custom = Json::deserialize<ThothCustom>(custom_it->second);
        if (custom.has_value()) {
            ignored = custom->ignored;
        }
    }
    auto thoth_info = ThothInfo { .id      = id_it->second,
                                  .os      = os_it == row.end() ? std::string {} : os_it->second,
                                  .token   = token_it->second,
                                  .ignored = std::move(ignored) };
    infos_[file_link].insert(thoth_info);
}

void ThothManager::legacy_network_thoth_record(const ActorId&     owner_id,
                                               const std::string& file_id,
                                               const DbRow&       row) {
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
    auto rows         = node->dfs()->read_vector_rows(owner_id,
                                              file_id,
                                              fmt::format("where status = '1' AND id = '{}'", id_it->second),
                                              security_key);
    if (!rows.has_value()) {
        return;
    }

    if (rows->empty()) {
        remove_thoth_info(id_it->second);
        return;
    }

    legacy_apply_thoth_row(rows->at(0));
}
