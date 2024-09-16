#ifndef BLOCKVARIANT_H
#define BLOCKVARIANT_H

#include <variant>

#include "datastorage/block.h"
#include "datastorage/genesis_block.h"

class BlockVariant {
public:
    explicit BlockVariant(std::variant<Block, GenesisBlock> block)
        : m_block(std::move(block)) {
    }

    explicit BlockVariant(Block block)
        : m_block(std::move(block)) {
    }
    explicit BlockVariant(GenesisBlock block)
        : m_block(std::move(block)) {
    }

    bool isEmpty() const;

    BlockType getType() const;
    BigNumber getIndex() const;
    std::string getData() const;
    std::string getPrevHash() const;
    std::string getHash() const;
    ActorId getApprover() const;
    std::string getSignature() const;
    QByteArrayList getListSignatures() const;
    std::vector<Transaction> transactions() const;

    std::string serialize() const;
    QString toString() const;

    void setType(BlockType type) {
        std::visit(
            [&type](auto& b) {
                b.setType(type);
            },
            m_block);
    }

    void setPrevHash(const std::string& prevHash) {
        std::visit(
            [&prevHash](auto& b) {
                b.setPrevHash(prevHash);
            },
            m_block);
    }

    void setData(const std::string& data) {
        std::visit(
            [&data](auto& b) {
                b.setData(data);
            },
            m_block);
    }

    void addSignature(const QByteArray& id, const QByteArray& sign, const bool& isApprover) {
        std::visit(
            [&id, &sign, &isApprover](auto& b) {
                b.addSignature(id, sign, isApprover);
            },
            m_block);
    }

    void sign(const std::shared_ptr<Actor<KeyPrivate>> actor) {
        std::visit(
            [actor](auto& b) {
                b.sign(actor);
            },
            m_block);
    }

    bool verify(const Actor<KeyPublic>& actor) const {
        return std::visit(
            [&actor](auto& b) {
                return b.verify(actor);
            },
            m_block);
    }

    bool isBlock() const {
        return std::holds_alternative<Block>(m_block);
    }

    bool isGenesisBlock() const {
        return std::holds_alternative<GenesisBlock>(m_block);
    }

    std::optional<std::reference_wrapper<const Block>> getBlockConst() const {
        if (auto block = std::get_if<Block>(&m_block)) {
            return std::cref(*block);
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<const GenesisBlock>> getGenesisBlockConst() const {
        if (auto block = std::get_if<GenesisBlock>(&m_block)) {
            return std::cref(*block);
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<Block>> getBlock() {
        if (auto block = std::get_if<Block>(&m_block)) {
            return std::ref(*block);
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<GenesisBlock>> getGenesisBlock() {
        if (auto block = std::get_if<GenesisBlock>(&m_block)) {
            return std::ref(*block);
        }
        return std::nullopt;
    }

    Block getAny() {
        if (isGenesisBlock())
            return getGenesisBlock()->get();
        return getBlock()->get();
    }

    bool operator==(const BlockVariant& other) const {
        if (isGenesisBlock()) {
            return getGenesisBlockConst() == other.getGenesisBlockConst();
        }
        return getBlockConst() == other.getBlockConst();
    }

private:
    std::variant<Block, GenesisBlock> m_block;
};

#endif // BLOCKVARIANT_H
