#ifndef PROFILE_H
#define PROFILE_H

#include <QObject>
#include <QDebug>
#include <QFile>

#include "dfs/types/headers/dfstruct.h"

class Profile
{
    Q_GADGET
    Q_PROPERTY(QString userId READ userId WRITE setUserId)
    Q_PROPERTY(qint16 type READ type WRITE setType)
    Q_PROPERTY(QString typeString READ typeString)
    Q_PROPERTY(QString firstName READ firstName WRITE setFirstName)
    Q_PROPERTY(QString lastName READ lastName WRITE setLastName)
    Q_PROPERTY(qint64 registerDate READ registerDate WRITE setRegisterDate)
    Q_PROPERTY(qint16 birthDay READ birthDay WRITE setBirthDay)
    Q_PROPERTY(qint16 birthMonth READ birthMonth WRITE setBirthMonth)
    Q_PROPERTY(qint16 birthYear READ birthYear WRITE setBirthYear)
    Q_PROPERTY(qint16 gender READ gender WRITE setGender)
    Q_PROPERTY(QStringList avatar READ avatar WRITE setAvatar)
    Q_PROPERTY(QString bio READ bio WRITE setBio)
    Q_PROPERTY(QString url READ url WRITE setUrl)
    Q_PROPERTY(QString urlName READ urlName WRITE setUrlName)
    Q_PROPERTY(QString ethereum READ ethereum WRITE setEthereum)

    Q_PROPERTY(quint16 country READ country WRITE setCountry)
    Q_PROPERTY(QString facebook READ facebook WRITE setFacebook)
    Q_PROPERTY(QString instagram READ instagram WRITE setInstagram)

    // TODO: subs
    Q_PROPERTY(QStringList portfolio READ portfolio WRITE setPortfolio)
    Q_PROPERTY(bool unit READ unit WRITE setUnit)
    Q_PROPERTY(QList<int> category READ category WRITE setCategory)
    Q_PROPERTY(QList<int> body READ body WRITE setBody)
    Q_PROPERTY(QList<int> hair READ hair WRITE setHair)
    Q_PROPERTY(QList<int> hairLength READ hairLength WRITE setHairLength)
    Q_PROPERTY(QList<int> eye READ eye WRITE setEye)
    Q_PROPERTY(QList<int> sizes READ sizes WRITE setSizes)
    Q_PROPERTY(QList<int> ethnicity READ ethnicity WRITE setEthnicity)
    Q_PROPERTY(QList<int> style READ style WRITE setStyle)
    Q_PROPERTY(QList<int> sports READ sports WRITE setSports)
    Q_PROPERTY(QList<int> skin READ skin WRITE setSkin)
    Q_PROPERTY(QList<int> scope READ scope WRITE setScope)
    Q_PROPERTY(QList<int> direction READ direction WRITE setDirection)
    Q_PROPERTY(QList<int> workStyle READ workStyle WRITE setWorkStyle)
    Q_PROPERTY(QList<int> fashion READ fashion WRITE setFashion)
    // Q_PROPERTY(QStringList pubs READ pubs)

public:
    Profile();
    Profile(const QByteArrayList& profile);
    Profile(const QByteArray& serialize); // needs for entity
    Profile(const Profile& profile);

    QByteArray serialize() const;

    operator QString() const;
    Q_INVOKABLE QString toString() const;
    QByteArrayList& list();
    const QByteArrayList getConstList() const;
    void setList(const QByteArrayList& value);
    void setValue(const QString& field, const QByteArray& value);
    Q_INVOKABLE bool isEmpty();
    Q_INVOKABLE QString value(const QString& field) const;
    QByteArray at(int index) const;
    int field(const QString& field) const;

    QString userId() const;
    qint16 type() const;
    QString typeString();
    QString firstName() const;
    QString lastName() const;
    qint64 registerDate() const;
    qint16 birthDay() const;
    qint16 birthMonth() const;
    qint16 birthYear() const;
    qint16 gender() const;
    QStringList avatar() const;
    qint16 country() const;
    QString bio() const;
    QString url() const;
    QString urlName() const;
    QString facebook() const;
    QString instagram() const;
    QString ethereum() const;

    QStringList portfolio();
    bool unit();
    QList<int> category();
    QList<int> body();
    QList<int> hair();
    QList<int> hairLength();
    QList<int> eye();
    QList<int> sizes();
    QList<int> ethnicity();
    QList<int> style();
    QList<int> sports();
    QList<int> skin();
    QList<int> scope();
    QList<int> direction();
    QList<int> workStyle();
    QList<int> fashion();
    QStringList pubs();

    void setUserId(const QString& userId);
    void setVersion(int version);
    void setType(qint16 type);
    void setFirstName(const QString& firstName);
    void setLastName(const QString& lastName);
    void setRegisterDate(qint64 registerDate);
    void setBirthDay(qint16 birthDay);
    void setBirthMonth(qint16 birthMonth);
    void setBirthYear(qint16 birthYear);
    void setGender(qint16 gender);
    void setAvatar(QStringList avatar);
    void setBio(const QString& bio);
    void setUrl(const QString& url);
    void setUrlName(const QString& urlName);
    void setCountry(qint16 country);
    void setFacebook(const QString& facebook);
    void setInstagram(const QString& instagram);
    void setEthereum(const QString& ethereum);

    void setPortfolio(QStringList portfolio);
    void setUnit(bool unit);
    void setCategory(QList<int> category);
    void setBody(QList<int> body);
    void setHair(QList<int> hair);
    void setHairLength(QList<int> hairLength);
    void setEye(QList<int> eye);
    void setSizes(QList<int> sizes);
    void setEthnicity(QList<int> ethnicity);
    void setStyle(QList<int> style);
    void setSports(QList<int> sports);
    void setSkin(QList<int> skin);
    void setScope(QList<int> scope);
    void setDirection(QList<int> direction);
    void setWorkStyle(QList<int> workStyle);
    void setFashion(QList<int> fashion);

    bool isServiceExists();

    enum Type
    {
        TypeWallet,
        TypeCustomer,
        TypeModel,
        TypeAgent,
        TypeAgency,
        TypeFashion,
        TypeContract
    };

    friend bool operator==(Profile& lhs, Profile& rhs)
    {
        return lhs.m_list == rhs.m_list;
    }

private:
    QList<int> toListInt(const QString& field);
    QString fromListInt(const QList<int>& list);

    QByteArrayList m_list;

public:
    static const QStringList fieldsCustomer;
    static const QStringList fieldsModel;
    static const QStringList fieldsAgent;
    static const QStringList fieldsAgency;
    static const QStringList fieldsFashion;
    static const QStringList fieldsToken;
    static const QMap<int, QString> unitMap;
    static const QMap<int, QString> categoryMap;
    static const QMap<int, QString> bodyMap;
    static const QMap<int, QString> hairMap;
    static const QMap<int, QString> hairLengthMap;
    static const QMap<int, QString> eyeMap;
    static const QMap<int, QString> ethnicityMap;
    static const QMap<int, QString> styleMap;
    static const QMap<int, QString> sportsMap;
    static const QMap<int, QString> skinMap;
    static const QMap<int, QString> scopeMap;
    static const QMap<int, QString> directionMap;
    static const QMap<int, QString> workStyleMap;
    static const QMap<int, QString> fashionMap;
};

inline bool operator==(const Profile& lhs, const Profile& rhs)
{
    return lhs.userId() == rhs.userId();
}

#endif // PROFILE_H
