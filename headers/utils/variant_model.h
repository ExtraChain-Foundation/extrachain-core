#ifndef VARIANT_MODEL_H
#define VARIANT_MODEL_H

#include <QAbstractListModel>
#include <QFile>
#include <QJsonDocument>
#include <QModelIndex>
#include <QVariant>

#include "extrachain_global.h"

class EXTRACHAIN_EXPORT VariantModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit VariantModel(QAbstractListModel *parent = nullptr, const QList<QByteArray> &list = {});

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    void setCount(int count);
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    Q_INVOKABLE void prepend(const QVariantMap &variant);
    Q_INVOKABLE void append(const QVariantMap &variant);
    Q_INVOKABLE void insert(int index, const QVariantMap &variant);
    Q_INVOKABLE void inserts(int index, const QVariantList &variant);
    Q_INVOKABLE void move(int from, int to, int n);
    Q_INVOKABLE void remove(int index, int count);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap get(int index);
    Q_INVOKABLE void set(int indx, const QByteArray &role, const QVariant &value);

    QList<QByteArray> modelRoles() const;
    void setModelRoles(const QList<QByteArray> &value);

    void appendFromJson(const QString &fileName);
    void insertFromJson(int index, const QString &fileName);
    QVariantMap loadJson(const QString &fileName);

    QList<QVariantMap> &list();

signals:
    void countChanged(int count);

private:
    QHash<int, QByteArray> m_roles;
    QByteArrayList m_modelRoles;
    QList<QVariantMap> m_datas;
    int m_count = 0;
};

#endif // VARIANT_MODEL_H
