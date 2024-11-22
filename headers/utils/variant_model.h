/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

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

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int                    count() const;
    void                   setCount(int count);
    QHash<int, QByteArray> roleNames() const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool                   setData(const QModelIndex &index, const QVariant &value, int role) override;

    Q_INVOKABLE void        prepend(const QVariantMap &variant);
    Q_INVOKABLE void        append(const QVariantMap &variant);
    Q_INVOKABLE void        insert(int index, const QVariantMap &variant);
    Q_INVOKABLE void        inserts(int index, const QVariantList &variant);
    Q_INVOKABLE void        move(int from, int to, int n);
    Q_INVOKABLE void        remove(int index, int count);
    Q_INVOKABLE void        clear();
    Q_INVOKABLE QVariantMap get(int index);
    Q_INVOKABLE void        set(int indx, const QByteArray &role, const QVariant &value);

    QList<QByteArray> modelRoles() const;
    void              setModelRoles(const QList<QByteArray> &value);

    void        appendFromJson(const QString &fileName);
    void        insertFromJson(int index, const QString &fileName);
    QVariantMap loadJson(const QString &fileName);

    QList<QVariantMap> &list();

signals:
    void countChanged(int count);

private:
    QHash<int, QByteArray> m_roles;
    QByteArrayList         m_modelRoles;
    QList<QVariantMap>     m_datas;
    int                    m_count = 0;
};
