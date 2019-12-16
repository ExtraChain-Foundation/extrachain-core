#include "dfs/types/headers/dfstruct.h"

dfsStruct::State dfsStruct::convertToDFSstate(QByteArray _state)
{
    if (QString(_state) == "NEWSTATE")
        return State::NEWSTATE;
    else if (QString(_state) == "DELSTATE")
        return State::DELSTATE;
    else
        return State::CHANGEDS;
}

QString dfsStruct::toString(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}

QByteArray dfsStruct::toByteArray(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}
//
dfsStruct::Status dfsStruct::convertToDFSstatus(QByteArray state)
{
    if (QString(state) == "NEW")
        return Status::NEW;
    else if (QString(state) == "REPLACE")
        return Status::REPLACE;
    else
        return Status::MERGE;
}

QString dfsStruct::toString(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
QByteArray dfsStruct::toByteArray(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
//
dfsStruct::Type dfsStruct::convertToDFType(QByteArray type)
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

QByteArray dfsStruct::toByteArray(Type type)
{
    QByteArray res;
    switch (type)
    {
    case dfsStruct::Type::images:
        res = "images";
        break;
    case dfsStruct::Type::video:
        res = "video";
        break;
    case dfsStruct::Type::event:
        res = "events";
        break;
    case dfsStruct::Type::system:
        res = "system";
        break;
    case dfsStruct::Type::chat:
        res = "chats";
        break;
    case dfsStruct::Type::post:
        res = "posts";
        break;
    case dfsStruct::Type::service:
        res = "services";
        break;
    default:
        return "";
    }
    return res;
}
QString dfsStruct::toString(Type type)
{

    return QString(dfsStruct::toByteArray(type));
}
//
dfsStruct::SubType dfsStruct::convertToDFSSubType(QByteArray subType)
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

QByteArray dfsStruct::toByteArray(SubType subType)
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
QString dfsStruct::toString(SubType subType)
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
dfsStruct::Key dfsStruct::convertToKey(QByteArray key)
{
    if (key == "dfsIndex")
        return Key::dfsIndex;
    else
        return Key::storedIndex;
}

QByteArray dfsStruct::toByteArray(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}

QString dfsStruct::toString(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}
