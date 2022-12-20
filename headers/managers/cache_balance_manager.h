#pragma once

#include <QObject>
#include "extrachain_node.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"

class CasheBalanceManager : public QObject {
    Q_OBJECT
    ExtraChainNode *node;
    DBConnector connector;
    std::vector<DFS::Balances::Balance> balances;

public:
    CasheBalanceManager(ExtraChainNode *node, QObject *parent = nullptr);

    void load();
    bool update(const std::string &actor_id, const std::string &balance);
    bool add(const std::string &actor_id, const std::string &balance);
    const std::vector<DFS::Balances::Balance> &getBalances() const;
    DFS::Balances::Balance getBalanceByActorId(const std::string actorId);
};
