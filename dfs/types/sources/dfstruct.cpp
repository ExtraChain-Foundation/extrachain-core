#include "dfs/types/headers/dfstruct.h"

DfsStruct::Type DfsStruct::convertToDFType(QByteArray type)
{
    if (type == "images")
        return Type::images;
    else if (type == "video")
        return Type::video;
    else if (type == "events")
        return Type::event;
    else if (type == "chats")
        return Type::chat;
    else if (type == "posts")
        return post;
    else if (type == "services")
        return service;
    else if (type == "files")
        return files;
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
    case DfsStruct::Type::chat:
        res = "chats";
        break;
    case DfsStruct::Type::post:
        res = "posts";
        break;
    case DfsStruct::Type::service:
        res = "services";
        break;
    case DfsStruct::Type::files:
        res = "files";
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
