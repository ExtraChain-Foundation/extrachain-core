#ifndef UI_MESSAGES_H
#define UI_MESSAGES_H
#include "dfs/types/headers/dfstruct.h"
namespace ui_messages {

enum page
{
    registation,
    logIn,
    profile,
    post,
    event,
    wallet,
    search,
    contract,
    images,
    miniAva
};
QByteArray toByteArray(page id);
QString toString(page id);
page convertToPage(const QByteArray &id);

const QMap<page, DfsStruct::Type> pageConnections = {
    { registation, DfsStruct::Type::Service },
    { miniAva, DfsStruct::Type::Service },
    { post, DfsStruct::Type::Post },
    { event, DfsStruct::Type::Event },
    { images, DfsStruct::Type::Image }
};
}
template <class T>
class UsersData
{
    T *data;

public:
    UsersData(const QByteArray &serialize)
    {
        data = new T(serialize);
    }
    ~UsersData()
    {
        delete data;
    }
    const T *getData() const
    {
        return data;
    }
    const QByteArray serialize() const
    {
        return data->serialize();
    }
};

#endif // UI_MESSAGES_H
