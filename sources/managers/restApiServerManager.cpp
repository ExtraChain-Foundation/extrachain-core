#include "managers/restApiServerManager.h"

RestApiServerManager::RestApiServerManager(ExtraChainNode *extrachainNode, QObject *parent)
    : QObject(parent) {
    _extrachainNode = extrachainNode;
    run();
}

void RestApiServerManager::handle_get_count_blocks(http_request request) {
    qDebug() << "\nhandle GET\n" << request.headers().size();
    auto answer = json::value::object();
    answer["count_blocks"] = json::value::number(
        std::stoi(_extrachainNode->blockchain()->getBlockChainLength().toStdString(10)));
    request.reply(status_codes::OK, answer);
}

void RestApiServerManager::handle_get_contents_block(http_request request) {
    qDebug() << "\nhandle GET contents block\n";

    auto queries = request.relative_uri().split_query(request.relative_uri().query());
    auto answer = json::value::object();
    auto it = queries.find("block_number");
    if (it->first.empty()) {
        qDebug() << "no block number";
        request.reply(status_codes::OK, answer);
        return;
    }

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

    auto queries = request.relative_uri().split_query(request.relative_uri().query());

    auto answer = json::value::object();
    auto it = queries.find("hash_tx");
    if (it->first.empty()) {
        qDebug() << "no hash tx";
        answer["answer"] = json::value::string("no hash_tx query");
        request.reply(status_codes::OK, answer);
        return;
    }

    const std::pair<Transaction, QByteArray> txByHash =
        _extrachainNode->blockchain()->getTxByHash(QByteArray::fromStdString(it->second));

    if(txByHash.second == "-1") {
        answer["answer"] = json::value::string("no transaction");
        request.reply(status_codes::OK, answer);
        return;
    }

    Transaction tx = txByHash.first;

    if(tx.isEmpty()) {
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

void RestApiServerManager::run() {
    bool listener = false;
    int port = 8100;
    while (!listener) {
        try {
            // count blocks
            listener_count_blocks = http_listener(
                QString("http://localhost:%1/extrachain/count_blocks").arg(port).toStdString());
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
            listener_contents_block =
                http_listener(QString("http://localhost:%1/extrachain/listener_contents_block")
                                  .arg(port)
                                  .toStdString());

            listener_contents_block.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_contents_block, this, std::placeholders::_1));
            listener_contents_block.open()
                .then([=]() { qDebug() << "\nstarting to listen contents block\n"; })
                .wait();

                   // transaction by hash
            listener_transactions_by_hash =
                http_listener(QString("http://localhost:%1/extrachain/listener_transactions_by_hash")
                                  .arg(port)
                                  .toStdString());
            listener_transactions_by_hash.support(
                methods::GET,
                std::bind(&RestApiServerManager::handle_get_transactions_by_hash, this,
                          std::placeholders::_1));

            listener_transactions_by_hash.open()
                .then([=]() { qDebug() << "\nstarting to listen transactions by hash\n"; })
                .wait();

            while (true)
                ;
        } catch (exception const &e) {
            wcout << e.what() << endl;
            port++;
        }
    }
}

