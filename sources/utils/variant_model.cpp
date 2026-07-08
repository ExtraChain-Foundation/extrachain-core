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

#include "utils/variant_model.h"
#include "utils/exc_logs.h"

VariantModel::VariantModel(QAbstractListModel *parent, const QList<QByteArray> &list)
    : QAbstractListModel(parent) {
    setModelRoles(list);
}

int VariantModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return m_datas.length();
}

int VariantModel::count() const {
    return m_count;
}

void VariantModel::setCount(int count) {
    if (m_count == count || count < 0)
        return;

    m_count = count;
    emit countChanged(m_count);
}

QHash<int, QByteArray> VariantModel::roleNames() const {
    return m_roles;
}

QVariant VariantModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_datas.length()) {
        return {};
    }

    const QVariantMap &variants = m_datas[index.row()];
    const QByteArray   roleName = m_roles.value(role);
    if (roleName.isEmpty()) {
        return {};
    }
    return variants.value(roleName);
}

bool VariantModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    set(index.row(), m_roles[role], value);

    return true;
}

QVariantList VariantModel::findByField(const QByteArray &field, const QVariant &value, bool firstMatchOnly) {
    QVariantList results;

    for (int i = 0; i < m_datas.size(); ++i) {
        const QVariantMap &item = m_datas[i];

        if (item.contains(field) && item[field] == value) {
            results.append(QVariant(item));

            if (firstMatchOnly) {
                break;
            }
        }
    }

    return results;
}

int VariantModel::findIndexByField(const QByteArray &field, const QVariant &value) {
    for (int i = 0; i < m_datas.size(); ++i) {
        const QVariantMap &item = m_datas[i];
        if (item.contains(field) && item[field] == value) {
            return i;
        }
    }
    return -1;
}

void VariantModel::prepend(const QVariantMap &variant) {
    insert(0, variant);
}

void VariantModel::append(const QVariantMap &variant) {
    // eLog("[VariantModel] Append {}", variant);
    insert(m_datas.length(), variant);
}

void VariantModel::appends(const QVariantList &variants) {
    if (variants.empty()) {
        return;
    }

    int startIndex = m_datas.size();
    beginInsertRows(QModelIndex(), startIndex, startIndex + variants.length() - 1);

    if (m_datas.size() + variants.size() > m_datas.capacity()) {
        m_datas.reserve(m_datas.size() + variants.size());
    }

    for (const auto &variant : variants) {
        m_datas.append(variant.toMap());
    }

    setCount(m_datas.length());
    endInsertRows();
}

void VariantModel::insert(int i, const QVariantMap &variant) {
    // Clamp: inserting past the end makes QList backfill empty maps → ghost rows.
    if (i < 0)
        i = 0;
    else if (i > m_datas.length())
        i = m_datas.length();

    beginInsertRows(QModelIndex(), i, i);
    m_datas.insert(i, variant);
    setCount(m_datas.length());
    endInsertRows();
}

void VariantModel::inserts(int i, const QVariantList &variants) {
    if (variants.empty()) {
        return;
    }

    if (i < 0)
        i = 0;
    else if (i > m_datas.length())
        i = m_datas.length();

    beginInsertRows(QModelIndex(), i, i + variants.length() - 1);

    if (m_datas.size() + variants.size() > m_datas.capacity()) {
        m_datas.reserve(m_datas.size() + variants.size());
    }

    int tempI = i;
    for (const auto &variant : variants) {
        m_datas.insert(tempI++, variant.toMap());
    }

    setCount(m_datas.length());
    endInsertRows();

    // emit dataChanged(index(i), index(i + variants.length() - 1));
}

void VariantModel::remove(int index, int count) {
    if (count <= 0 || index < 0 || index >= m_datas.count()) {
        return;
    }

    beginRemoveRows(QModelIndex(), index, index + count - 1);
    while (count--)
        m_datas.removeAt(index);
    endRemoveRows();
    setCount(m_datas.count());
}

QVariantMap VariantModel::get(int index) {
    if (index < 0 || index >= m_datas.size())
        return {};
    return m_datas[index];
}

void VariantModel::set(int indx, const QByteArray &role, const QVariant &value) {
    if (indx < 0 || indx >= m_datas.length()) {
        return;
    }
    auto &val = m_datas[indx];
    val[role] = value;
    emit dataChanged(index(indx, 0), index(indx, 0), QList<int>() << m_roles.key(role));
}

void VariantModel::move(int from, int to, int n) {
    if (from < 0 || from >= m_count || to < 0 || to >= m_count || n <= 0 || from + n > m_count)
        return;

    if (!beginMoveRows(QModelIndex(), from, from + n - 1, QModelIndex(), to > from ? to + 1 : to)) {
        return;
    }

    if (to > from) {
        for (int i = 0; i < n; i++) {
            m_datas.move(from, to - n + 1 + i);
        }
    } else if (to < from) {
        for (int i = 0; i < n; i++) {
            m_datas.move(from + i, to + i);
        }
    }

    endMoveRows();
}

QList<QByteArray> VariantModel::modelRoles() const {
    return m_modelRoles;
}

void VariantModel::setModelRoles(const QList<QByteArray> &value) {
    m_modelRoles = value;

    int roleCount = Qt::UserRole;
    for (auto &&role : m_modelRoles)
        m_roles[++roleCount] = role;
}

void VariantModel::appendFromJson(const QString &fileName) {
    QFile file(fileName);

    if (!file.exists())
        return;

    if (file.open(QFile::ReadOnly)) {
        QString json = file.readAll();
        auto    doc  = QJsonDocument::fromJson(json.toUtf8());
        auto    var  = doc.toVariant().toMap();
        append(var);
        file.close();
    }
}

void VariantModel::insertFromJson(int index, const QString &fileName) {
    QFile file(fileName);

    if (!file.exists())
        return;

    if (file.open(QFile::ReadOnly)) {
        QString json = file.readAll();
        auto    doc  = QJsonDocument::fromJson(json.toUtf8());
        auto    var  = doc.toVariant().toMap();
        insert(index, var);
        file.close();
    }
}

QVariantMap VariantModel::loadJson(const QString &fileName) {
    QFile       file(fileName);
    QVariantMap map;

    if (!file.exists()) {
        return map;
    }

    if (file.open(QFile::ReadOnly)) {
        QString json = file.readAll();
        map          = QJsonDocument::fromJson(json.toUtf8()).toVariant().toMap();
        file.close();
    }

    return map;
}

const QList<QVariantMap> &VariantModel::list() const {
    return m_datas;
}

void VariantModel::clear() {
    if (m_datas.isEmpty()) {
        return;
    }

    beginRemoveRows(QModelIndex(), 0, m_datas.size() - 1);
    m_datas.clear();
    m_count = 0;
    endRemoveRows();

    emit countChanged(m_count);
}
