/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "dfs/types/headers/dfstruct.h"

DfsStruct::Type DfsStruct::toDfsType(QByteArray type)
{
    if (type == "images")
        return DfsStruct::Type::Image;
    else if (type == "video")
        return DfsStruct::Type::Video;
    else if (type == "events")
        return DfsStruct::Type::Event;
    else if (type == "chats")
        return DfsStruct::Type::Chat;
    else if (type == "posts")
        return DfsStruct::Type::Post;
    else if (type == "services")
        return DfsStruct::Type::Service;
    else if (type == "files")
        return DfsStruct::Type::Files;
    else if (type == "contract")
        return DfsStruct::Type::Contract;
    else if (type == "private")
        return DfsStruct::Type::Private;
    return DfsStruct::Type::Service;
}

QByteArray DfsStruct::toByteArray(Type type)
{
    QByteArray res;
    switch (type)
    {
    case DfsStruct::Type::Image:
        res = "images";
        break;
    case DfsStruct::Type::Video:
        res = "video";
        break;
    case DfsStruct::Type::Event:
        res = "events";
        break;
    case DfsStruct::Type::Chat:
        res = "chats";
        break;
    case DfsStruct::Type::Post:
        res = "posts";
        break;
    case DfsStruct::Type::Service:
        res = "services";
        break;
    case DfsStruct::Type::Files:
        res = "files";
        break;
    case DfsStruct::Type::Contract:
        res = "contract";
        break;
    case DfsStruct::Type::Stored:
        res = "stored";
        break;
    case DfsStruct::Type::Private:
        res = "private";
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
