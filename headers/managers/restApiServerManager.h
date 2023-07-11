#pragma once

#include "datastorage/blockchain.h"
#include "managers/extrachain_node.h"
#include <QObject>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <QTimer>

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;
using namespace std;

static std::string COUNT_BLOCKS = "/extrachain/count_blocks";
static std::string CONTENT_BLOCK = "extrachain/listener_contents_block";
static std::string TRANSACTIONS_BY_HASH = "extrachain/listener_transactions_by_hash";
static std::string COUNT_REAL_BLOCKS = "extrachain/listener_count_real_blocks";
static std::string TRANSACTIONS_IN_BLOCKS = "extrachain/listener_transactions_in_blocks";

class RestApiServerManager : public QObject {
    Q_OBJECT

    http_listener listener_count_blocks;
    http_listener listener_contents_block;
    http_listener listener_transactions_by_hash;
    http_listener listener_count_real_blocks;
    http_listener listener_transactions_in_blocks;

    ExtraChainNode *_extrachainNode;

    std::map<std::string, int> mapOfHistory;
    std::multimap<std::string, std::vector<http_request>> listOfNoAnswerRequests;

public:
    explicit RestApiServerManager(ExtraChainNode *extrachainNode, QObject *parent = nullptr);

    void handle_get_count_blocks(http_request request);
    void handle_get_contents_block(http_request request);
    void handle_get_transactions_by_hash(http_request request);
    void handle_get_count_real_blocks(http_request request);
    void handle_get_count_transactions_in_blocks(http_request request);

private slots:
    void runRequests();

private:
    void run();
    QTimer *timer;
    inline void actorIdRequest(http_request request, json::value answer);
    const int ANSWER_INTERVAL_IN_SEC = 3;
};
