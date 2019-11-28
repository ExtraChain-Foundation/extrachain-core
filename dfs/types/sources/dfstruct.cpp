#include "dfs/types/headers/dfstruct.h"

based_dfs_struct::State based_dfs_struct::convertToDFSstate(QByteArray _state)
{
    if (QString(_state) == "NEWSTATE")
        return State::NEWSTATE;
    else if (QString(_state) == "DELSTATE")
        return State::DELSTATE;
    else
        return State::CHANGEDS;
}

QString based_dfs_struct::toString(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}

QByteArray based_dfs_struct::toByteArray(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}
//
based_dfs_struct::Status based_dfs_struct::convertToDFSstatus(QByteArray state)
{
    if (QString(state) == "NEW")
        return Status::NEW;
    else if (QString(state) == "REPLACE")
        return Status::REPLACE;
    else
        return Status::MERGE;
}

QString based_dfs_struct::toString(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
QByteArray based_dfs_struct::toByteArray(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
//
based_dfs_struct::Type based_dfs_struct::convertToDFType(QByteArray type)
{
    if (type == "images")
        return Type::images;
    else if (type == "video")
        return Type::video;
    else if (type == "events")
        return Type::event;
    else if (type == "system")
        return Type::system;
    else if (type == "chats")
        return Type::chat;
    else if (type == "posts")
        return post;
    else if (type == "cards")
        return card;
    else if (type == "services")
        return service;
    else if (type == "cdoctp")
        return cdoctp;
    return service;
}

QByteArray based_dfs_struct::toByteArray(Type type)
{
    QByteArray res;
    switch (type)
    {
    case based_dfs_struct::Type::images:
        res = "images";
        break;
    case based_dfs_struct::Type::video:
        res = "video";
        break;
    case based_dfs_struct::Type::event:
        res = "events";
        break;
    case based_dfs_struct::Type::system:
        res = "system";
        break;
    case based_dfs_struct::Type::chat:
        res = "chats";
        break;
    case based_dfs_struct::Type::post:
        res = "posts";
        break;
    case based_dfs_struct::Type::service:
        res = "services";
        break;
    default:
        return "";
    }
    return res;
}
QString based_dfs_struct::toString(Type type)
{

    return QString(based_dfs_struct::toByteArray(type));
}
//
based_dfs_struct::SubType based_dfs_struct::convertToDFSSubType(QByteArray subType)
{
    if (subType == "profil")
        return profil;
    else if (subType == "avatar")
        return avatar;
    else if (subType == "ipost")
        return ipost;
    else if (subType == "mini")
        return mini;
    else if (subType == "portfolio")
        return portfolio;
    else
        return ievent;
}

QByteArray based_dfs_struct::toByteArray(SubType subType)
{
    if (subType == profil)
        return "profil";
    else if (subType == avatar)
        return "avatar";
    else if (subType == ipost)
        return "ipost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "ievent";
}
QString based_dfs_struct::toString(SubType subType)
{
    if (subType == profil)
        return "profil";
    else if (subType == avatar)
        return "avatar";
    else if (subType == ipost)
        return "ipost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "ievent";
}
based_dfs_struct::Key based_dfs_struct::convertToKey(QByteArray key)
{
    if (key == "dfsIndex")
        return Key::dfsIndex;
    else
        return Key::storedIndex;
}

QByteArray based_dfs_struct::toByteArray(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}

QString based_dfs_struct::toString(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}
