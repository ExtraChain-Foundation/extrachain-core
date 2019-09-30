#ifndef PUBLIC_PROFILE_H
#define PUBLIC_PROFILE_H
#include "datastorage/profile.h"
#include "utils/utils.h"
struct indexList
{
    indexList(long long curPos, int _size);
    long long currentPosition;
    int size;
};

class PublicProfile
{
public:
    PublicProfile(Profile _profile, QByteArray _sign, QString path);
    PublicProfile(Profile _profile, QByteArray _sign);
    PublicProfile(const QByteArray &serialize);
    PublicProfile();
    QByteArray serialize() const;
    static Profile saveProfile(Profile newProfile, const QString &path, QByteArray sign);
    static PublicProfile getProfile(const QString &path, const QString id);
    static QByteArray serialize(QByteArrayList actorList);
    static QByteArrayList deserialize(QByteArray serializeData);
    Profile profile;
    QByteArray sign = "";
signals:
    //
private:
    static void saveTokenNames(QByteArray id, QByteArray nameToken);
    //    QList<indexList> index;
};
#endif // PUBLIC_PROFILE_H
