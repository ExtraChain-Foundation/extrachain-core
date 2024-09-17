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

std::set<std::string> BlockVariant::dataService() const {
    return std::visit(
        [](const auto& b) {
            return b.dataService();
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

std::string BlockVariant::getSignature() const {
    return std::visit(
        [](const auto& b) {
            return b.getSignature();
        },
        m_block);
}

std::set<Approver> BlockVariant::signatures() const {
    return std::visit(
        [](const auto& b) {
            return b.signatures();
        },
        m_block);
}

std::set<Transaction> BlockVariant::transactions() const {
    if (isGenesisBlock()) {
        qDebug() << "[BlockVariant] Try to get transactions for GenesisBlock";
    }

    return std::visit(
        [](const auto& b) {
            return b.transactions();
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

std::string BlockVariant::toStdString() const {
    return std::visit(
        [](const auto& b) {
            return b.toStdString();
        },
        m_block);
}

void BlockVariant::setType(BlockType type) {
    std::visit(
        [&type](auto& b) {
            b.setType(type);
        },
        m_block);
}

void BlockVariant::setPrevHash(const std::string& prevHash) {
    std::visit(
        [&prevHash](auto& b) {
            b.setPrevHash(prevHash);
        },
        m_block);
}

void BlockVariant::addSignature(const std::string& id, const std::string& sign, bool isApprove) {
    std::visit(
        [&id, &sign, &isApprove](auto& b) {
            b.addSignature(id, sign, isApprove);
        },
        m_block);
}

void BlockVariant::sign(const std::shared_ptr<Actor<KeyPrivate>> actor) {
    std::visit(
        [&actor](auto& b) {
            b.sign(actor);
        },
        m_block);
}

bool BlockVariant::verify(const Actor<KeyPublic>& actor) const {
    return std::visit(
        [&actor](auto& b) {
            return b.verify(actor);
        },
        m_block);
}

bool BlockVariant::isBlock() const {
    return std::holds_alternative<Block>(m_block);
}

bool BlockVariant::isGenesisBlock() const {
    return std::holds_alternative<GenesisBlock>(m_block);
}

std::optional<std::reference_wrapper<const Block>> BlockVariant::getBlockConst() const {
    if (auto block = std::get_if<Block>(&m_block)) {
        return std::cref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const GenesisBlock>> BlockVariant::getGenesisBlockConst() const {
    if (auto block = std::get_if<GenesisBlock>(&m_block)) {
        return std::cref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<Block>> BlockVariant::getBlock() {
    if (auto block = std::get_if<Block>(&m_block)) {
        return std::ref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<GenesisBlock>> BlockVariant::getGenesisBlock() {
    if (auto block = std::get_if<GenesisBlock>(&m_block)) {
        return std::ref(*block);
    }
    return std::nullopt;
}

Block BlockVariant::getAny() {
    if (isGenesisBlock())
        return getGenesisBlock()->get();
    return getBlock()->get();
}
