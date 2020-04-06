#ifndef QUICKPROFILE_H
#define QUICKPROFILE_H

#include <QObject>
#include <QVariantMap>
#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

struct QuickProfile
{
    Q_GADGET
    Q_PROPERTY(QString firstName MEMBER firstName)
    Q_PROPERTY(QString lastName MEMBER lastName)
    Q_PROPERTY(QString avatar MEMBER avatar)

public:
    QString userId;
    QString firstName;
    QString lastName;
    QString avatar;

    operator QString() const // for QDebug
    {
        return QString("first name: %1, last name: %2, avatar: %3").arg(firstName, lastName, avatar);
    }
};

#endif // QUICKPROFILE_H
