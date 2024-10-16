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
    std::set<std::string> dataService() const;
    std::string getPrevHash() const;
    std::string getPrevGenHash() const;
    std::string getHash() const;
    std::string getSignature() const;
    std::set<Approver> signatures() const;
    std::set<Transaction> transactions() const;
    const GenesisDataRows &dataRows() const;

    QString toString() const;
    std::string toStdString() const;

    void setType(BlockType type);
    void setPrevHash(const std::string& prevHash);

    void addSignature(const std::string& id, const std::string& sign, bool isApprove);
    void sign(const std::shared_ptr<Actor<KeyPrivate>> actor);
    bool verify(const Actor<KeyPublic>& actor) const;

    bool isBlock() const;
    bool isGenesisBlock() const;

    std::optional<std::reference_wrapper<const Block>> getBlockConst() const;
    std::optional<std::reference_wrapper<const GenesisBlock>> getGenesisBlockConst() const;
    std::optional<std::reference_wrapper<Block>> getBlock();
    std::optional<std::reference_wrapper<GenesisBlock>> getGenesisBlock();

    bool operator==(const BlockVariant& other) const {
        if (isGenesisBlock()) {
            return getGenesisBlockConst() == other.getGenesisBlockConst();
        }
        return getBlockConst() == other.getBlockConst();
    }

private:
    std::variant<Block, GenesisBlock> m_block;
};


QDebug operator<<(QDebug debug, const BlockVariant &block);

#endif // BLOCKVARIANT_H
