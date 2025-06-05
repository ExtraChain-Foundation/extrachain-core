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

#include "managers/subscription_manager.h"

#include "dfs/dfs_controller.h"

SubscriptionManager::SubscriptionManager(ExtraChainNode* node)
    : node(node) {
}

bool SubscriptionManager::create_subscription_vector(const ActorId&     tariff_owner_id,
                                                     std::string&       tariff_file_id,
                                                     const std::string& file_name) {
    auto network_id = node->actorIndex()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(network_id,
                                                                  Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                  "Subscription");
    if (!search_result.has_value()) {
        return false;
    }

    auto system_actor_id = node->accountController()->system_actor().id();
    auto sub_res =
        node->dfs()->store_vector(system_actor_id, system_actor_id, file_name, network_id, search_result->file_id);
    if (!sub_res.has_value()) {
        return false;
    }

    // save tariffs file link json
    auto file_link = Dfs::FileLink { .owner_id = tariff_owner_id, .file_id = tariff_file_id };
    auto json      = Json::serialize(file_link);

    auto res = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTIONS,
                                               file_name,
                                               Dfs::DataSecurity::Public);

    return true;
}

std::optional<std::vector<SubscriptionTariff>> SubscriptionManager::read_tariffs(const ActorId&     owner_id,
                                                                                 const std::string& file_name) {
    auto file_path = Dfs::Path::file_path(owner_id, "");
    if (!file_path.has_value()) {
        return std::nullopt;
    }

    auto content = Utils::read_file_content(file_path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    auto tariffs = Json::deserialize<std::vector<SubscriptionTariff>>(content.value());
    if (!tariffs.has_value()) {
        return std::nullopt;
    }

    return tariffs.value();
}

bool SubscriptionManager::add_subscription(const ActorId&     owner_id,
                                           const std::string& file_id,
                                           int                type,
                                           bool               auto_renew,
                                           const TokenId&     token_id) {
    if (subscription_row.has_value()) {
        return false;
    }

    ActorId system_id = node->accountController()->system_actor().id();

    Transaction transaction;
    transaction.set_type(TransactionType::Repeatable);
    transaction.set_sender(system_id);
    transaction.set_receiver(owner_id);
    transaction.set_amount(BigNumberFloat("500", NumeralBase::Dec)); // TODO: get from tariff
#ifdef QT_DEBUG
    transaction.set_amount(BigNumberFloat("1.123", NumeralBase::Dec));
#endif
    transaction.set_token(token_id); // TODO: get token_id from json
    transaction.set_meta(Json::serialize(Dfs::FileLink { .owner_id = owner_id, .file_id = file_id }));

    node->send_transaction(transaction, node->accountController()->system_actor());

    auto row         = SubscriptionRow { .owner_id = owner_id, .file_id = file_id, .type = type };
    subscription_row = row;

    return true;
}

void SubscriptionManager::self_tx_repeatable_added(const Transaction& transaction) {
    if (!subscription_row.has_value()) {
        return;
    }

    ActorId system_id = node->accountController()->system_actor().id();
    if (transaction.sender() != system_id) {
        return;
    }

    auto row = subscription_row.value();

    row.section_id       = transaction.section();
    row.date_start       = transaction.timestamp();
    row.transaction_hash = transaction.hash();

    auto row_map = Utils::to_dbrow(row);

    // temp for old vector
    auto section = row_map["section_id"];
    row_map.erase("section_id");
    row_map.insert({ "block_id", section });

    auto res = node->dfs()->add_vector_row(row.owner_id, row.file_id, row_map, system_id);

    if (res) {
        emit node->subscriptionAdded(row.owner_id, row.file_id);
    }
}

bool SubscriptionManager::create_tafiffs_file(const std::string&                    file_name,
                                              const std::vector<SubscriptionTariff> tariffs) {
    auto system_actor_id = node->accountController()->system_actor().id();
    auto json            = Json::serialize(tariffs);

    auto res = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTIONS,
                                               file_name,
                                               Dfs::DataSecurity::Public);
    return res.has_value();
}
