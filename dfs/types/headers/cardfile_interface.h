#ifndef CARDFILE_INTERFACE_H
#define CARDFILE_INTERFACE_H
#include "dfs/types/headers/dfstruct.h"
class ICardFile
{
    // delimetr wich will be use in all card file
    const QByteArray CARD_FILE_DELIMETR = "|";
    const QByteArray CARD_FILE_FILEDS_DELIMETR = ":";
    const QByteArray CARD_FILE_ELEMENTS_DELIMETR = ",";

    const short FIELDS_SIZE = 4;
    const short HASH_SIZE = 64;

private:
    // index of hash position for card file special type
    std::vector<std::tuple<int, std::string, long long>> indexList;
    //
    QFile cardFile;

    // read hash from card file
    std::string getHash() const;

public:
    ICardFile(const BigNumber userId);
    ~ICardFile();
};

#endif // CARDFILE_INTERFACE_H
