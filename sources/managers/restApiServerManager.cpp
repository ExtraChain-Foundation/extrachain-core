#include "managers/restApiServerManager.h"

RestApiServerManager::RestApiServerManager(ExtraChainNode *extrachainNode, QObject *parent)
    : QObject(parent)
    , timer(new QTimer(this))
    , COUNT_BLOCKS("/extrachain/count_blocks")
    , CONTENT_BLOCK("/extrachain/listener_contents_block")
    , TRANSACTIONS_BY_HASH("/extrachain/listener_transactions_by_hash")
    , COUNT_REAL_BLOCKS("/extrachain/listener_count_real_blocks")
    , TRANSACTIONS_IN_BLOCKS("/extrachain/listener_transactions_in_blocks") {
    _extrachainNode = extrachainNode;
    timer->setInterval(ANSWER_INTERVAL_IN_SEC);
    connect(timer, &QTimer::timeout, this, &RestApiServerManager::runRequests);
    run();
}

void RestApiServerManager::handle_get_count_blocks(http_request request) {
    qDebug() << "\nhandle GET\n" << request.headers().size();

    auto answer = json::value::object();
    actorIdRequest(request, answer);
    qDebug() << "repeat send answer request without check actor id";
    answer["count_blocks"] =
        json::value::number(std::stoi(_extrachainNode->blockchain()->getRecords().toStdString(10)));

    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::handle_get_contents_block(http_request request) {
    qDebug() << "\nhandle GET contents block\n";

    auto answer = json::value::object();
    auto queries = request.relative_uri().split_query(request.relative_uri().query());
    auto it = queries.find("block_number");
    if (it->first.empty()) {
        qDebug() << "no block number";
        answer["answer"] = json::value::string("no block number");
        request.reply(status_codes::OK, answer);
        return;
    }

    actorIdRequest(request, answer);

    Block block = _extrachainNode->blockchain()->getBlockByIndex(BigNumber(it->second, 10));

    answer["index"] = json::value::string(block.getIndex().toStdString(10));
    answer["hash"] = json::value::string(block.getHash());
    answer["prev_hash"] = json::value::string(block.getPrevHash());
    answer["data"] = json::value::string(block.getData());
    answer["dig_sig"] = json::value::string(block.getDigSig());
    answer["type"] = json::value::string(block.getType());
    answer["approver"] = json::value::string(block.getApprover().toStdString());
    answer["date"] = json::value::number(block.getDate());

    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::handle_get_transactions_by_hash(http_request request) {
    qDebug() << "\nhandle GET transactions by hash\n" << request.headers().size();

    auto answer = json::value::object();
    auto queries = request.relative_uri().split_query(request.relative_uri().query());

    auto it = queries.find("hash_tx");
    if (it->first.empty()) {
        qDebug() << "no hash tx";
        answer["answer"] = json::value::string("no hash_tx query");
        request.reply(status_codes::OK, answer);
        return;
    }

    actorIdRequest(request, answer);

    const std::pair<Transaction, QByteArray> txByHash =
        _extrachainNode->blockchain()->getTxByHash(QByteArray::fromStdString(it->second));

    if (txByHash.second == "-1") {
        answer["answer"] = json::value::string("no transaction");
        request.reply(status_codes::OK, answer);
        return;
    }

    Transaction tx = txByHash.first;

    if (tx.isEmpty()) {
        qDebug() << "no block number";
        answer["answer"] = json::value::string("transaction is empty");
        request.reply(status_codes::OK, answer);
        return;
    }
    answer["hash"] = json::value::string(tx.getHash());
    answer["data_from_hash"] = json::value::string(tx.getDataForHash());
    answer["data"] = json::value::string(tx.getData());
    answer["amount"] = json::value::string(tx.getAmount().toStdString(10));
    answer["approver"] = json::value::string(tx.getApprover().toStdString());
    answer["date"] = json::value::number(tx.getDate());
    answer["gas"] = json::value::number(tx.getGas());
    answer["hop"] = json::value::number(tx.getHop());
    answer["prev_block"] = json::value::string(tx.getPrevBlock().toStdString(10));
    answer["producer"] = json::value::string(tx.getProducer().toStdString());
    answer["receiver"] = json::value::string(tx.getReceiver().toStdString());
    answer["sender"] = json::value::string(tx.getSender().toStdString());
    answer["type_tx"] = json::value::number(tx.getTypeTx() == TypeTx::Transaction ? 0 : 1);

    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::handle_get_count_real_blocks(http_request request) {
    qDebug() << "\nhandle GET\n" << request.headers().size();
    auto answer = json::value::object();
    actorIdRequest(request, answer);
    answer["count_real_blocks"] = json::value::number(
        std::stoi(_extrachainNode->blockchain()->getCountRealBlockRecords().toStdString(10)));
    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::handle_get_count_transactions_in_blocks(http_request request) {
    qDebug() << "\nhandle GET\n" << request.headers().size();
    auto answer = json::value::object();
    actorIdRequest(request, answer);
    answer["count_transactions_in_blocks"] =
        json::value::number(_extrachainNode->blockchain()->getCountTransactionsInBlocks());
    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::setIntervalRequestReply(const int &interval) {
    timer->setInterval(interval);
}

int RestApiServerManager::port() const {
    return listeningPort;
}

void RestApiServerManager::runRequests() {
    qDebug() << "Size mapOfNoAnswerRequests: " << listOfNoAnswerRequests.size();
    for (const auto request : listOfNoAnswerRequests) {
        qDebug() << request.first.c_str() << request.second.size();
        auto requests = request.second;
        for (int i = 0; i < requests.size(); i++) {
            auto request = requests[i];
            std::string requestUri = requests[i].request_uri().to_string();
            if (requestUri.starts_with(COUNT_BLOCKS)) {
                handle_get_count_blocks(request);
            }

            if (requestUri.starts_with(CONTENT_BLOCK)) {
                handle_get_contents_block(request);
            }

            if (requestUri.starts_with(TRANSACTIONS_BY_HASH)) {
                handle_get_transactions_by_hash(request);
            }

            if (requestUri.starts_with(COUNT_REAL_BLOCKS)) {
                handle_get_count_real_blocks(request);
            }

            if (requestUri.starts_with(TRANSACTIONS_IN_BLOCKS)) {
                handle_get_count_transactions_in_blocks(request);
            }
        }
    }

    listOfNoAnswerRequests.clear();
    timer->stop();
}

void RestApiServerManager::run() {
    bool listener = false;
    int port = 8100;
    while (!listener) {
        try {
            // count blocks
            listener_count_blocks = http_listener(
                QString("http://localhost:%1%2").arg(port).arg(COUNT_BLOCKS.c_str()).toStdString());
            listener_count_blocks.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_count_blocks, this, std::placeholders::_1));

            listener_count_blocks.open()
                .then([=, &listener]() {
                    qDebug() << "\nstarting to listen count block\n";
                    qDebug() << listener_count_blocks.uri().to_string().c_str();
                    listener = true;
                })
                .wait();

                   // data block
            listener_contents_block = http_listener(
                QString("http://localhost:%1%2").arg(port).arg(CONTENT_BLOCK.c_str()).toStdString());

            listener_contents_block.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_contents_block, this, std::placeholders::_1));
            listener_contents_block.open()
                .then([=]() { qDebug() << "\nstarting to listen contents block\n"; })
                .wait();

                   // transaction by hash
            listener_transactions_by_hash = http_listener(
                QString("http://localhost:%1%2").arg(port).arg(TRANSACTIONS_BY_HASH.c_str()).toStdString());
            listener_transactions_by_hash.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_transactions_by_hash, this,
                          std::placeholders::_1));

            listener_transactions_by_hash.open()
                .then([=]() { qDebug() << "\nstarting to listen transactions by hash\n"; })
                .wait();

                   // count real blocks
            listener_count_real_blocks = http_listener(
                QString("http://localhost:%1%2").arg(port).arg(COUNT_REAL_BLOCKS.c_str()).toStdString());

            listener_count_real_blocks.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_count_real_blocks, this, std::placeholders::_1));

            listener_count_real_blocks.open()
                .then([=]() { qDebug() << "\nstarting to listen count real blocks\n"; })
                .wait();

                   // count transactions in blocks
            listener_transactions_in_blocks = http_listener(
                QString("http://localhost:%1%2").arg(port).arg(TRANSACTIONS_IN_BLOCKS.c_str()).toStdString());

            listener_transactions_in_blocks.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_count_transactions_in_blocks, this,
                          std::placeholders::_1));

            listener_transactions_in_blocks.open()
                .then([=]() { qDebug() << "\nstarting to listen count transactions in blocks\n"; })
                .wait();

            listeningPort = port;

            while (true)
                ;
        } catch (exception const &e) {
            wcout << e.what() << endl;
            port++;
        }
    }
}

void RestApiServerManager::actorIdRequest(http_request request, json::value answer) {
    auto queries = request.relative_uri().split_query(request.relative_uri().query());
    auto actorIdIt = queries.find("actor_id");
    if (actorIdIt->first.empty()) {
        qDebug() << "no actor id param";
        answer["answer"] = json::value::string("no actor_id query");
        request.reply(status_codes::OK, answer);
        return;
    }

    std::string actorId = actorIdIt->second;
    auto delayed_request = queries.find("is_delayed");
    if (!delayed_request->first.empty() && std::stoi(delayed_request->second) == 1) {
        qDebug() << "Current request is delayed";
        std::vector<http_request> requests;
        requests.push_back(std::move(request));
        listOfNoAnswerRequests.insert(
            std::pair<std::string, std::vector<http_request>>(actorId, requests));
        answer["answer"] = json::value::string("query is delayed. Answer will be later.");
        request.reply(status_codes::OK, answer);
        return;
    }

    int currentSecsSinceEpoch = QDateTime::currentSecsSinceEpoch();
    auto historyItem = mapOfHistory.find(actorId);
    if (!historyItem->first.empty()) {
        // update
        int lastRequestTime = historyItem->second;
        qDebug() << "update" << currentSecsSinceEpoch << lastRequestTime
                 << (currentSecsSinceEpoch - lastRequestTime)
                 << ((currentSecsSinceEpoch - lastRequestTime) > ANSWER_INTERVAL_IN_SEC);

        if ((currentSecsSinceEpoch - lastRequestTime) > ANSWER_INTERVAL_IN_SEC) {
            // allow make answer and update time
            mapOfHistory[actorId] = currentSecsSinceEpoch;
        } else {
            // add into mapOfNoAnswerRequests
            if (listOfNoAnswerRequests.find(actorId) != listOfNoAnswerRequests.end()) {
                // find
                std::vector<http_request> vec = listOfNoAnswerRequests.find(actorId)->second;
                bool founded = false;
                for (int i = 0; i < vec.size(); i++) {
                    if (vec[i].request_uri() == request.request_uri())
                        founded = true;
                };
                if (!founded)
                    listOfNoAnswerRequests.find(actorId)->second.push_back(request);
            } else {
                std::vector<http_request> requests;
                requests.push_back(request);
                listOfNoAnswerRequests.insert(
                    std::pair<std::string, std::vector<http_request>>(actorId, requests));
            }

            timer->start();
            return;
        }
    } else {
        // insert to history request
        qDebug() << "insert to history request";
        mapOfHistory.insert(std::pair<std::string, int>(actorId, currentSecsSinceEpoch));
    }
}
