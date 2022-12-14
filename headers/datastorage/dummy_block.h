#ifndef DUMMY_BLOCK_H
#define DUMMY_BLOCK_H

#include "datastorage/block.h"

class EXTRACHAIN_EXPORT DummyBlock : public Block {
public:
    DummyBlock()
        : Block() {
        this->m_type = Config::DUMMY_BLOCK_TYPE;
    }

    explicit DummyBlock(const QByteArray &serialized) {
        deserialize(serialized);
    };

    DummyBlock(const Block &prevBlock, const Block &lastRealBlock)
        : Block(QByteArray::fromStdString(lastRealBlock.getHash()), prevBlock)
        , m_data(QByteArray::fromStdString(lastRealBlock.getHash())) {
        setType(Config::DUMMY_BLOCK_TYPE);
    };

    static bool isDummyBlock(const QByteArray &serialized) {
        return serialized.contains(Config::DUMMY_BLOCK_TYPE);
    };

    bool deserialize(const QByteArray &serialized) override {
        *this = MessagePack::deserialize<DummyBlock>(serialized);
        return true;
    }

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = index.toStdString();
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures)
            .msgpack_pack(msgpack_pk);
    }
    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string index_str;
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures)
            .msgpack_unpack(msgpack_o);
        index = index_str;
    }

public:
    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    virtual std::string getDataForHash() const override {
        return Block::getDataForHash();
    };
    virtual const std::string &getDataForDigSig() const override {
        return Block::getDataForDigSig();
    };

    void initFields(QList<QByteArray> &list) override {
        return Block::initFields(list);
    };

private:
    QByteArray m_data;
};

#endif // DUMMY_BLOCK_H
