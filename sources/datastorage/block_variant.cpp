#include "datastorage/block_variant.h"

bool BlockVariant::isEmpty() const {
    return std::visit(
        [](const auto& b) {
            return b.isEmpty();
        },
        m_block);
}

BlockType BlockVariant::getType() const {
    return std::visit(
        [](const auto& b) {
            return b.getType();
        },
        m_block);
}

BigNumber BlockVariant::getIndex() const {
    return std::visit(
        [](const auto& b) {
            return b.getIndex();
        },
        m_block);
}

std::string BlockVariant::getData() const {
    return std::visit(
        [](const auto& b) {
            return b.getData();
        },
        m_block);
}

std::string BlockVariant::getPrevHash() const {
    return std::visit(
        [](const auto& b) {
            return b.getPrevHash();
        },
        m_block);
}

std::string BlockVariant::getHash() const {
    return std::visit(
        [](const auto& b) {
            return b.getHash();
        },
        m_block);
}

ActorId BlockVariant::getApprover() const {
    return std::visit(
        [](const auto& b) {
            return b.getApprover();
        },
        m_block);
}

std::string BlockVariant::getSignature() const {
    return std::visit(
        [](const auto& b) {
            return b.getSignature();
        },
        m_block);
}

QByteArrayList BlockVariant::getListSignatures() const {
    return std::visit(
        [](const auto& b) {
            return b.getListSignatures();
        },
        m_block);
}

std::vector<Transaction> BlockVariant::transactions() const {
    if (isGenesisBlock()) {
        qDebug() << "[BlockVariant] Try to get transactions for GenesisBlock";
    }

    return std::visit(
        [](const auto& b) {
            return b.transactions();
        },
        m_block);
}

std::string BlockVariant::serialize() const {
    return std::visit(
        [](const auto& b) {
            return b.serialize();
        },
        m_block);
}

QString BlockVariant::toString() const {
    return std::visit(
        [](const auto& b) {
            return b.toString();
        },
        m_block);
}
