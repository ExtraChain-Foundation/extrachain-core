#include "utils/variant_model.h"

#include <QDebug>

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
    if (m_count == count)
        return;

    m_count = count;
    emit countChanged(m_count);
}

QHash<int, QByteArray> VariantModel::roleNames() const {
    return m_roles;
}

QVariant VariantModel::data(const QModelIndex &index, int role) const {
    QVariantMap variants = m_datas[index.row()];
    return variants[m_roles[role]];
}

bool VariantModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    set(index.row(), m_roles[role], value);

    return true;
}

void VariantModel::prepend(const QVariantMap &variant) {
    insert(0, variant);
}

void VariantModel::append(const QVariantMap &variant) {
    // qDebug() << "append" << variant;
    insert(m_datas.length(), variant);
}

void VariantModel::insert(int i, const QVariantMap &variant) {
    beginInsertRows(QModelIndex(), i, i);

    m_datas.insert(i, variant);

    endInsertRows();
    setCount(m_datas.length());
}

void VariantModel::inserts(int i, const QVariantList &variants) {
    beginInsertRows(QModelIndex(), i, i + variants.length() - 1);

    int tempI = i;
    for (auto &&variant : variants)
        m_datas.insert(tempI++, variant.toMap());

    setCount(m_datas.length());

    endInsertRows();
    emit dataChanged(index(i), index(i + variants.length() - 1));
}

void VariantModel::remove(int index, int count = 0) {
    beginRemoveRows(QModelIndex(), index, index + count - 1);
    while (count--)
        m_datas.removeAt(index);
    endRemoveRows();
    setCount(m_datas.count());
}

QVariantMap VariantModel::get(int index) {
    // qDebug() << "GET" << index << m_count - 1 << (index > m_count - 1);
    if (index > m_count - 1 || index < 0)
        return {};
    return m_datas[index];
}

void VariantModel::set(int indx, const QByteArray &role, const QVariant &value) {
    auto &val = m_datas[indx];
    val[role] = value;
    emit dataChanged(index(indx, 0), index(indx, 0), QList<int>() << m_roles.key(role));
}

void VariantModel::move(int from, int to, int n) {
    if (from >= m_count || from < 0 || to < 0 || to >= m_count
        || !beginMoveRows(QModelIndex(), from, from + n - 1, QModelIndex(), to > from ? to + 1 : to))
        return;
    if (n > 1 && from + n < to && to + n < m_count) {
        qDebug() << "n > 1";
        for (int i = 0; i < n; i++)
            m_datas.move(from + i, to + i);
    } else
        m_datas.move(from, to);
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
        auto doc = QJsonDocument::fromJson(json.toUtf8());
        auto var = doc.toVariant().toMap();
        var["alphabet"] = "";
        append(var);
    }

    file.close();
}

void VariantModel::insertFromJson(int index, const QString &fileName) {
    QFile file(fileName);

    if (!file.exists())
        return;

    if (file.open(QFile::ReadOnly)) {
        QString json = file.readAll();
        auto doc = QJsonDocument::fromJson(json.toUtf8());
        auto var = doc.toVariant().toMap();
        var["alphabet"] = "";
        insert(index, var);
    }

    file.close();
}

QVariantMap VariantModel::loadJson(const QString &fileName) {
    QFile file(fileName);
    QVariantMap map;

    if (file.open(QFile::ReadOnly)) {
        QString json = file.readAll();
        map = QJsonDocument::fromJson(json.toUtf8()).toVariant().toMap();
        file.close();
    }

    return map;
}

QList<QVariantMap> &VariantModel::list() {
    return m_datas;
}

void VariantModel::clear() {
    beginResetModel();
    m_datas.clear();
    setCount(0);
    endResetModel();
}
