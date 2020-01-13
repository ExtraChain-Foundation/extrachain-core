#include "dfs/types/headers/dfstruct.h"

DfsStruct::State DfsStruct::convertToDFSstate(QByteArray _state)
{
    if (QString(_state) == "NEWSTATE")
        return State::NEWSTATE;
    else if (QString(_state) == "DELSTATE")
        return State::DELSTATE;
    else
        return State::CHANGEDS;
}

QString DfsStruct::toString(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}

QByteArray DfsStruct::toByteArray(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}

DfsStruct::Type DfsStruct::convertToDFType(QByteArray type)
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

QByteArray DfsStruct::toByteArray(Type type)
{
    QByteArray res;
    switch (type)
    {
    case DfsStruct::Type::images:
        res = "images";
        break;
    case DfsStruct::Type::video:
        res = "video";
        break;
    case DfsStruct::Type::event:
        res = "events";
        break;
    case DfsStruct::Type::system:
        res = "system";
        break;
    case DfsStruct::Type::chat:
        res = "chats";
        break;
    case DfsStruct::Type::post:
        res = "posts";
        break;
    case DfsStruct::Type::service:
        res = "services";
        break;
    case DfsStruct::Type::cdoctp:
        res = "cdoctp";
        break;
    case DfsStruct::Type::card:
        res = "cards";
        break;
    case DfsStruct::Type::contract:
        res = "contract";
        break;
    case DfsStruct::Type::stored:
        res = "stored";
        break;
    default:
        return "";
    }
    return res;
}
QString DfsStruct::toString(Type type)
{

    return QString(DfsStruct::toByteArray(type));
}
//
DfsStruct::SubType DfsStruct::convertToDFSSubType(QByteArray subType)
{
    if (subType == "profile")
        return profile;
    else if (subType == "avatar")
        return avatar;
    else if (subType == "subpost")
        return subpost;
    else if (subType == "mini")
        return mini;
    else if (subType == "portfolio")
        return portfolio;
    else
        return subevent;
}

QByteArray DfsStruct::toByteArray(SubType subType)
{
    if (subType == profile)
        return "profile";
    else if (subType == avatar)
        return "avatar";
    else if (subType == subpost)
        return "subpost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "subevent";
}
QString DfsStruct::toString(SubType subType)
{
    if (subType == profile)
        return "profile";
    else if (subType == avatar)
        return "avatar";
    else if (subType == subpost)
        return "subpost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "subevent";
}
DfsStruct::Key DfsStruct::convertToKey(QByteArray key)
{
    if (key == "dfsIndex")
        return Key::dfsIndex;
    else
        return Key::storedIndex;
}

QByteArray DfsStruct::toByteArray(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}

QString DfsStruct::toString(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}
