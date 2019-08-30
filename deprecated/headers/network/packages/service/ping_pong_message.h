#ifndef PING_PONG_MESSAGE_H
#define PING_PONG_MESSAGE_H

#include <QByteArray>
#include <QJsonDocument>
#include "utils/utils.h"

namespace Messages {
static const QByteArray PING_MESSAGE = "ping ExtraNet";
static const QByteArray PONG_MESSAGE = "pong ExtraNet";

static QByteArray createPingMessage()
{
    QByteArray msg("{");
    msg.append("\"proto\":\"")
        .append(Config::Net::PROTOCOL_VERSION)
        .append("\",")
        .append("\"type\":\"")
        .append(PING_MESSAGE)
        .append("\"")
        .append("}");

    return msg;
}

static QByteArray createPongMessage(const quint16 port)
{
    QByteArray msg("{");
    msg.append("\"proto\":\"")
        .append(Config::Net::PROTOCOL_VERSION)
        .append("\",")
        .append("\"type\":\"")
        .append(PONG_MESSAGE)
        .append("\",")
        .append("\"netPort\":\"")
        .append(QByteArray::number(port))
        .append("\"")
        .append("}");

    return msg;
}

/**
 * @brief Check message type (Only for simple messaes like ping-pong)
 * @param msg - serialized message
 * @param type - type field
 * @return true, if proto and type is good
 */
static bool checkType(const QByteArray &msg, const QString &type)
{
    QJsonDocument doc = QJsonDocument::fromJson(msg);
    return !doc.isNull()
        && doc.object().value("proto").toString() == Config::Net::PROTOCOL_VERSION
        && doc.object().value("type").toString() == type;
}

static bool isPing(const QByteArray &msg)
{
    return Messages::checkType(msg, PING_MESSAGE);
}

static bool isPong(const QByteArray &msg)
{
    return Messages::checkType(msg, PONG_MESSAGE);
}
}

#endif // PING_PONG_MESSAGE_H
