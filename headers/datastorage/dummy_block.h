#ifndef DUMMY_BLOCK_H
#define DUMMY_BLOCK_H

#include "datastorage/block.h"
#include "datastorage/genesis_block.h"

class EXTRACHAIN_EXPORT DummyDataRow {
public:
    ActorId actorId;
    BigNumber state;
    ActorId token;
    DataStorage::typeDataRow type;

public:
    DummyDataRow() = default;

    explicit DummyDataRow(const ActorId &actorId, const BigNumber &state, const ActorId &token,
                          const DataStorage::typeDataRow &type)
        : actorId(actorId)
        , state(state)
        , token(token)
        , type(type) {
    }

    explicit DummyDataRow(const QByteArray &serialized) {
        deserialize(serialized);
    }

    QByteArray serialize() const {
        QList<QByteArray> l;
        l << actorId.toByteArray() << state.toByteArray() << token.toByteArray() << QByteArray::number(type);
        return Serialization::serialize(l, Serialization::DEFAULT_FIELD_SIZE);
    }

    void deserialize(const QByteArray &serialized) {
        QList<QByteArray> l = Serialization::deserialize(serialized, Serialization::DEFAULT_FIELD_SIZE);
        if (l.size() == 4) {
            actorId = l.at(0).toStdString();
            state = BigNumber(l.at(1).toStdString());
            token = l.at(2).toStdString();
            type = DataStorage::typeDataRow(l.at(3).toInt());
        }
    }

    bool operator==(const DummyDataRow &other) const {
        return this->actorId == other.actorId && this->state == other.state && this->token == other.token
            && this->type == other.type;
    }
};

namespace Config {
static const std::string DUMMY_BLOCK_TYPE = "dummy";
}

class EXTRACHAIN_EXPORT DummyBlock : public Block {
public:
    std::string prevGenHash; // previous genesis block hashes

public:
    DummyBlock();
    DummyBlock(const DummyBlock &block);

    // Deserialize already constructed block
    explicit DummyBlock(const QByteArray &serialized);

    // Initial block construction, prev = nullptr for first block
    explicit DummyBlock(const Block &prevBlock);

    // Block interface
public:
    void addRow(const DummyDataRow &row);
    QByteArray getDataForHash() const override;           // deprecate?
    const std::string &getDataForDigSig() const override; // deprecate?
    bool deserialize(const QByteArray &serialized) override;
    QByteArray serialize() const override;
    void initFields(QList<QByteArray> &list) override;

    /**
     * @brief extract non-empty genesisDataRows from data
     * @return genesis data row list
     */
    QList<DummyDataRow> extractDataRows() const;
    static bool isDummyBlock(const QByteArray &serialized);

public:
    std::string getPrevGenHash() const;
    void setPrevGenHash(const std::string &value);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = index.toStdString();
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures,
                                         prevGenHash)
            .msgpack_pack(msgpack_pk);
    }
    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string index_str;
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures,
                                         prevGenHash)
            .msgpack_unpack(msgpack_o);
        index = index_str;
    }
};

#endif // DUMMY_BLOCK_H
