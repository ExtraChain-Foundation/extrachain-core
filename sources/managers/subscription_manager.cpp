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

bool SubscriptionManager::create_subscription_vector(const ActorId&     plan_owner_id,
                                                     std::string&       plan_file_id,
                                                     const std::string& subscription_name) {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto db = Dfs::Tables::DirsFile::DirsSpace::database();
    if (!db.has_value()) {
        return false;
    }

    auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), network_id, Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE, "SubscriptionTemplate");
    if (!search_result.has_value()) {
        return false;
    }

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto sub_res         = node->dfs()->store_vector(system_actor_id,
                                             system_actor_id,
                                             subscription_name,
                                             network_id,
                                             search_result->file_id);
    if (!sub_res.has_value()) {
        return false;
    }

    // save plans file link json
    auto file_link = Dfs::FileLink { .owner_id = plan_owner_id, .file_id = plan_file_id };
    auto json      = Json::serialize(file_link);

    auto res = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTION,
                                               subscription_name,
                                               Dfs::DataSecurity::Public);

    return true;
}

std::optional<std::pair<std::string, std::string>> SubscriptionManager::is_subscription_prepared(
    const ActorId&     owner_id,
    const std::string& subscription_name) {

    auto db = Dfs::Tables::DirsFile::DirsSpace::database();
    if (!db.has_value()) {
        return std::nullopt;
    }

    auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), owner_id, Dfs::Basic::TEMPLATE_VECTOR, subscription_name);
    if (!search_result.has_value()) {
        return std::nullopt;
    }
    if (search_result->state != Dfs::FileState::Ready) {
        return std::nullopt;
    }

    auto search_result_info = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), owner_id, Dfs::Basic::TEMPLATE_SUBSCRIPTION, subscription_name);

    if (!search_result_info.has_value()) {
        return std::nullopt;
    }
    if (search_result_info->state != Dfs::FileState::Ready) {
        return std::nullopt;
    }

    return std::pair { search_result->file_id, search_result_info->file_id };
}

std::optional<std::unordered_map<std::string, SubscriptionPlan>> SubscriptionManager::read_plans(
    const ActorId&     owner_id,
    const std::string& subscription_name) {
    auto file_path = Dfs::Path::file_path(owner_id, "");
    if (!file_path.has_value()) {
        return std::nullopt;
    }

    auto content = Utils::read_file_content(file_path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    auto plans = Json::deserialize<std::unordered_map<std::string, SubscriptionPlan>>(content.value());
    if (!plans.has_value()) {
        return std::nullopt;
    }

    return plans.value();
}

bool SubscriptionManager::add_subscription(const ActorId&     owner_id,
                                           const std::string& subscription_name,
                                           const std::string& plan_id) {
    if (subscription_row.has_value()) { // TODO: make map for waiting
        return false;                   // TODO: expected
    }

    auto plans = read_plans(owner_id, subscription_name);
    if (plans->find(plan_id) == plans->end()) {
        return false;
    }

    auto plan = plans->at(plan_id);

    ActorId system_id = node->account_controller()->system_actor().id();

    Transaction transaction;
    transaction.set_type(TransactionType::Repeatable);
    transaction.set_sender(system_id);
    transaction.set_receiver(owner_id);
    transaction.set_amount(BigNumberFloat(plan.price, NumeralBase::Dec)); // TODO: get from plan
#ifdef QT_DEBUG
    transaction.set_amount(BigNumberFloat("1.123", NumeralBase::Dec));
#endif
    transaction.set_token(plan.token_id); // TODO: get token_id from json
    transaction.set_meta(
        Json::serialize(Dfs::FileLink { .owner_id = owner_id, .file_id = subscription_row->file_id }));

    node->send_transaction(transaction, node->account_controller()->system_actor());

    auto row = SubscriptionRow { .owner_id = owner_id, .file_id = subscription_row->file_id, .plan_id = plan_id };
    subscription_row = row;

    return true;
}

void SubscriptionManager::self_tx_repeatable_added(const Transaction& transaction) {
    if (!subscription_row.has_value()) {
        return;
    }

    ActorId system_id = node->account_controller()->system_actor().id();
    if (transaction.sender() != system_id) {
        return;
    }

    auto row = subscription_row.value();

    row.date_start       = transaction.timestamp();
    row.section_id       = transaction.section();
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

bool SubscriptionManager::create_subscription_template() {
    auto subscription_template = Dfs::CollectionTemplate::create("Subscription")
                                     .value()
                                     .add_fields({ Dfs::Field::String("plan_id").not_null(),
                                                   Dfs::Field::Integer("date_start").not_null(),
                                                   Dfs::Field::String("section_id").not_null(),
                                                   Dfs::Field::String("transaction_hash").not_null() });

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto template_res    = node->dfs()->store_template(system_actor_id, subscription_template);
    if (!template_res.has_value()) {
        eCritical("Can't create subscription template, because {}", template_res.error());
        return false;
    }

    return true;
}

std::expected<Dfs::DirRow, Dfs::DfsError> SubscriptionManager::create_plans(
    const std::string&                  file_name,
    const std::vector<SubscriptionPlan> plans) {

    std::unordered_map<std::string, SubscriptionPlan> plans_map;
    for (const auto& plan : plans) {
        plans_map.insert({ Utils::generate_random_hex(6), plan });
    }

    auto json            = Json::serialize(plans_map);
    auto system_actor_id = node->account_controller()->system_actor().id();
    auto res             = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTION,
                                               file_name,
                                               Dfs::DataSecurity::Public);
    return res;
}
