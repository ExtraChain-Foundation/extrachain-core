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

    void setType(BlockType type);
    void setPrevHash(const std::string& prevHash);

    void addSignature(const QByteArray& id, const QByteArray& sign, const bool& isApprover);
    void sign(const Actor<KeyPrivate>& actor);
    bool verify(const Actor<KeyPublic>& actor) const;

    bool isBlock() const;
    bool isGenesisBlock() const;

    std::optional<std::reference_wrapper<const Block>> getBlockConst() const;
    std::optional<std::reference_wrapper<const GenesisBlock>> getGenesisBlockConst() const;
    std::optional<std::reference_wrapper<Block>> getBlock();
    std::optional<std::reference_wrapper<GenesisBlock>> getGenesisBlock();
    Block getAny();

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
