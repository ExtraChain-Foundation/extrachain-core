#ifndef DUMMY_BLOCK_H
#define DUMMY_BLOCK_H

#include "datastorage/block.h"

class EXTRACHAIN_EXPORT DummyBlock : public Block {
public:
    explicit DummyBlock(const QByteArray &prev_block_hash);

private:
    QByteArray m_data;
};

#endif // DUMMY_BLOCK_H
