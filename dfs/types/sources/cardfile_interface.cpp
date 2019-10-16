#ifndef CARDFILE_INTERFACE_CPP
#define CARDFILE_INTERFACE_CPP

#include "utils/utils.h"
#include "dfs/types/headers/cardfile_interface.h"

#endif // CARDFILE_INTERFACE_CPP

ICardFile::ICardFile(const BigNumber userId)
{
    cardFile.setFileName(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId());
    cardFile.open(QIODevice::ReadWrite);
    long long first_data = Utils::qByteArrayToInt(cardFile.read(FIELDS_SIZE));
}

ICardFile::~ICardFile()
{
    cardFile.flush();
    cardFile.close();
}
