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

const QMap<page, based_dfs_struct::Type> pageConnections = {
    { registation, based_dfs_struct::Type::service },
    { miniAva, based_dfs_struct::Type::service },
    { post, based_dfs_struct::Type::post },
    { event, based_dfs_struct::Type::event },
    { images, based_dfs_struct::Type::images }
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
