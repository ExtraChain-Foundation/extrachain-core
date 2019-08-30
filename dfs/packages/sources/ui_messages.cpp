#include "dfs/packages/headers/ui_messages.h"

using namespace ui_messages;
QByteArray ui_messages::toByteArray(page id)
{
    if (id == event)
        return "event";
    else if (id == wallet)
        return "wallet";
    else if (id == search)
        return "search";
    else if (id == post)
        return "post";
    else if (id == profile)
        return "profile";
    else if (id == logIn)
        return "logIn";
    else if (id == images)
        return "images";
    else if (id == miniAva)
        return "miniAva";
    else
        return "registation";
}

QString ui_messages::toString(page id)
{
    if (id == event)
        return "event";
    else if (id == wallet)
        return "wallet";
    else if (id == search)
        return "search";
    else if (id == post)
        return "post";
    else if (id == profile)
        return "profile";
    else if (id == logIn)
        return "logIn";
    else if (id == images)
        return "images";
    else if (id == miniAva)
        return "miniAva";
    else
        return "registation";
}

page ui_messages::convertToPage(const QByteArray &id)
{
    if (id == "event")
        return event;
    else if (id == "wallet")
        return wallet;
    else if (id == "search")
        return search;
    else if (id == "post")
        return post;
    else if (id == "profile")
        return profile;
    else if (id == "logIn")
        return logIn;
    else if (id == "images")
        return images;
    else if (id == "miniAva")
        return miniAva;
    else
        return registation;
}
