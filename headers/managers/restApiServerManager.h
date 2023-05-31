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

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;
using namespace std;

class RestApiServerManager : public QObject {
    Q_OBJECT

    http_listener listener_count_blocks;
    http_listener listener_contents_block;
    http_listener listener_transactions_by_hash;

    ExtraChainNode *_extrachainNode;

public:
    explicit RestApiServerManager(ExtraChainNode *extrachainNode, QObject *parent = nullptr);

    void handle_get_count_blocks(http_request request);
    void handle_get_contents_block(http_request request);
    void handle_get_transactions_by_hash(http_request request);

private:
    void run();
};
