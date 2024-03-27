#pragma once

#include "datastorage/blockchain.h"
#include "managers/extrachain_node.h"
#include <QObject>
#include <QTimer>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;
using namespace std;

class RestApiServerManager : public QObject {
    Q_OBJECT

    ExtraChainNode *_extrachainNode;
    QTimer *timer;

    http_listener listener_count_blocks;
    http_listener listener_contents_block;
    http_listener listener_transactions_by_hash;
    http_listener listener_count_real_blocks;
    http_listener listener_transactions_in_blocks;
    std::map<std::string, int> mapOfHistory;
    std::multimap<std::string, std::vector<http_request>> listOfNoAnswerRequests;
    const int ANSWER_INTERVAL_IN_SEC = 3;

    const std::string COUNT_BLOCKS;
    const std::string CONTENT_BLOCK;
    const std::string TRANSACTIONS_BY_HASH;
    const std::string COUNT_REAL_BLOCKS;
    const std::string TRANSACTIONS_IN_BLOCKS;
    int listeningPort;

public:
    explicit RestApiServerManager(ExtraChainNode *extrachainNode, QObject *parent = nullptr);
    ~RestApiServerManager() {
        listener_count_blocks.close();
        listener_contents_block.close();
        listener_transactions_by_hash.close();
        listener_count_real_blocks.close();
        listener_transactions_in_blocks.close();
    }

    void handle_get_count_blocks(http_request request);
    void handle_get_contents_block(http_request request);
    void handle_get_transactions_by_hash(http_request request);
    void handle_get_count_real_blocks(http_request request);
    void handle_get_count_transactions_in_blocks(http_request request);
    void setIntervalRequestReply(const int &interval);
    int port() const;

private slots:
    void runRequests();

protected:
    void run();
    inline void actorIdRequest(http_request request, json::value answer);
};
