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
    if (QString(type) == "images")
        return Type::images;
    else if (QString(type) == "ivideo")
        return Type::ivideo;
    else if (QString(type) == "events")
        return Type::events;
    else if (QString(type) == "system")
        return Type::system;
    else if (QString(type) == "chates")
        return Type::chates;
    else if (type == "postes")
        return postes;
    else if (type == "card")
        return card;
    else
        return Type::servic;
}

QByteArray based_dfs_struct::toByteArray(Type type)
{
    if (type == Type::images)
        return "images";
    else if (type == Type::ivideo)
        return "ivideo";
    else if (type == Type::events)
        return "events";
    else if (type == Type::system)
        return "system";
    else if (type == Type::chates)
        return "chates";
    else if (type == postes)
        return "postes";
    else if (type == card)
        return "card";
    else
        return "servic";
}
QString based_dfs_struct::toString(Type type)
{
    if (type == Type::images)
        return "images";
    else if (type == Type::ivideo)
        return "ivideo";
    else if (type == Type::events)
        return "events";
    else if (type == Type::system)
        return "system";
    else if (type == Type::chates)
        return "chates";
    else if (type == postes)
        return "postes";
    else if (type == card)
        return "card";
    else
        return "servic";
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
