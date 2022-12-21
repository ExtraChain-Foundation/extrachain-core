#include "managers/cache_balance_manager.h"
#include <QDebug>

CasheBalanceManager::CasheBalanceManager(ExtraChainNode *node, QObject *parent)
    : QObject(parent)
    , node(node)
    , connector(DFS::Balances::balanceDbPath) {
    connector.open();
    connector.createTable(DFS::Balances::createBalanceTable);
    connector.close();
    load();
}

void CasheBalanceManager::load() {
    connector.open();
    if (!connector.isOpen())
        return;

    balances.clear();
    std::vector<DBRow> balanceQueryResult = connector.select(DFS::Balances::loadBalancesQuery);
    connector.close();
    if (balanceQueryResult.size() == 0) {
        qInfo() << "Balances empty";
        return;
    }

    for (const auto &balanceRow : balanceQueryResult) {
        std::string actor = balanceRow.at("actor_id");
        std::string balance = balanceRow.at("balance");
        balances.push_back(DFS::Balances::Balance { .Actor = actor, .Balance = balance });
    }
}

bool CasheBalanceManager::update(const std::string &actor_id, const std::string &balance) {
    connector.open();
    if (!connector.isOpen()) {
        qFatal("DB Balance not opened");
        return false;
    }
    auto newLastUpdate = QDateTime::currentMSecsSinceEpoch();
    const bool result = connector.query(
        fmt::format("UPDATE balances SET balance = '{}', last_update = '{}' WHERE actor_id = '{}'", balance,
                    std::to_string(newLastUpdate), actor_id));
    connector.close();
    return result;
}

bool CasheBalanceManager::add(const std::string &actor_id, const std::string &balance) {
    connector.open();
    if (!connector.isOpen()) {
        qFatal("DB Balance not opened");
        return false;
    }
    auto newLastUpdate = QDateTime::currentMSecsSinceEpoch();
    DBRow dbRow = { { "actor_id", actor_id },
                    { "balance", balance },
                    { "last_update", std::to_string(newLastUpdate) } };

    const bool result = connector.insert(DFS::Balances::balancesTableName, dbRow);
    connector.close();
    if(result)
        balances.push_back(DFS::Balances::Balance{.Actor = actor_id, .Balance = balance});
    return result;
}

const std::vector<DFS::Balances::Balance> &CasheBalanceManager::getBalances() const {
    return balances;
}

DFS::Balances::Balance CasheBalanceManager::getBalanceByActorId(const std::string actorId) {
    connector.open();
    if (!connector.isOpen()) {
        qFatal("DB Balance not opened");
        return DFS::Balances::Balance {};
    }

    std::vector<DBRow> result = connector.select(
        fmt::format("SELECT * FROM {} WHERE actor_id='{}'", DFS::Balances::balancesTableName, actorId));
    connector.close();
    qDebug()<< result.size();
    if (result.empty())
        return DFS::Balances::Balance {};

    DBRow data = result[0];
    std::string actor = data.at("actor_id");
    std::string balance = data.at("balance");

    return DFS::Balances::Balance { .Actor = actor, .Balance = balance };
}
