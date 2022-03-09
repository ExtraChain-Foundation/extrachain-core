#include "datastorage/blockchain/blockchain.h"

Blockchain2::Blockchain2(AccountController &accountController) {
    //
}

Blockchain2::~Blockchain2() {
    //
}

int Blockchain2::addBlock(const Block &block, bool isGenesis) {
    //    if (isGenesis) {
    //        qDebug() << "Adding a GENESIS block" << block.getIndex() << "to storage";
    //    } else {
    //        qDebug() << "Adding a block" << block.getIndex() << "to storage";
    //    }
    //    if (!GenesisBlock::isGenesisBlock(block.serialize())) {
    //        if (block.getIndex() != 0) {
    //            BigNumber id = block.getIndex() - 1;
    //            if (getBlock(SearchEnum::BlockParam::Id, id.toByteArray()).isEmpty()) {
    //                Messages::GetBlockMessage request;
    //                request.param = SearchEnum::BlockParam::Id;
    //                request.value = id.toByteArray();
    //                emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    //            }
    //        }
    //    }
    //    if (block.getIndex() == 0) {
    //        this->actorIndex->setFirstId(block.getApprover());
    //    }
    //    if (block.getIndex() < 0)
    //        return Errors::BLOCK_IS_NOT_VALID;
    //    if (signCheckAdd(block))
    //        emit sendMessage(block.serialize(), Messages::ChainMessage::BlockMessage);

    // HERE!!!
    int resultCode = fileMode ? blockIndex.addBlock(block) : memIndex.addBlock(block);

    switch (resultCode) {
    case 0: {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "Block" << block.getIndex() << QByteArray::fromStdString(block.getType())
                 << "is successfully added to blockchain";
        getSmContractMembers(block);

        emit sendMessage(block.serialize(), Messages::ChainMessage::BlockMessage);
        saveTxInfoInEC(QByteArray::fromStdString(block.getData()));
        stakingReward(block);
        break;
    }
    case Errors::FILE_ALREADY_EXISTS: {
        qDebug() << "Block" << block.getIndex() << "is already in blockchain";
        if ((block.getType() == Config::DATA_BLOCK_TYPE) || block.getType() == Config::MERGE_BLOCK) {
            resultCode = mergeBlockWithLocal(block);
        } else if ((block.getType() == Config::GENESIS_BLOCK_TYPE)
                   || (block.getType() == Config::GENESIS_BLOCK_MERGE)) {
            resultCode = mergeGenesisBlockWithLocal(dynamic_cast<const GenesisBlock &>(block));
        } else {
            qCritical() << "Unsupported block type in block: " << block.getIndex();
        }
        break;
    }
    default:
        qCritical() << "While adding a new block" << block.toString();
    }

    // after adding genesis block we don't need to increment counter
    if (!isGenesis && resultCode == 0) {
        blocksFromLastGenesis++;
        if (shouldStartGenesisCreation()) {
            GenesisBlock gB = createGenesisBlock(accountController->mainActor());
            if (blockIndex.addBlock(gB) == 0) {
                qDebug() << "Block" << gB.getIndex() << QByteArray::fromStdString(gB.getType())
                         << "is successfully added to blockchain";
                emit sendMessage(gB.serialize(), Messages::ChainMessage::GenesisBlockMessage);
                blocksFromLastGenesis = 0;
            }
        }
    }

    return resultCode;
}
