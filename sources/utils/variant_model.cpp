#include "utils/variant_model.h"

#include <QDebug>

VariantModel::VariantModel(QAbstractListModel *parent, const QList<QByteArray> &list)
    : QAbstractListModel(parent) {
    setModelRoles(list);
}

int VariantModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return datas.length();
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
    return roles;
}

QVariant VariantModel::data(const QModelIndex &index, int role) const {
    QVariantMap variants = datas[index.row()];
    return variants[roles[role]];
}

bool VariantModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    set(index.row(), roles[role], value);

    return true;
}

void VariantModel::prepend(const QVariantMap &variant) {
    insert(0, variant);
}

void VariantModel::append(const QVariantMap &variant) {
    // qDebug() << "append" << variant;
    insert(datas.length(), variant);
}

void VariantModel::insert(int i, const QVariantMap &variant) {
    beginInsertRows(QModelIndex(), i, i);

    datas.insert(i, variant);

    endInsertRows();
    setCount(datas.length());
}

void VariantModel::inserts(int i, const QVariantList &variants) {
    beginInsertRows(QModelIndex(), i, i + variants.length() - 1);

    int tempI = i;
    for (auto &&variant : variants)
        datas.insert(tempI++, variant.toMap());

    setCount(datas.length());

    endInsertRows();
    emit dataChanged(index(i), index(i + variants.length() - 1));
}

void VariantModel::remove(int index, int count = 0) {
    beginRemoveRows(QModelIndex(), index, index + count - 1);
    while (count--)
        datas.removeAt(index);
    endRemoveRows();
    setCount(datas.count());
}

QVariantMap VariantModel::get(int index) {
    // qDebug() << "GET" << index << m_count - 1 << (index > m_count - 1);
    if (index > m_count - 1 || index < 0)
        return QVariantMap();
    return datas[index];
}

void VariantModel::set(int indx, const QByteArray &role, const QVariant &value) {
    auto &val = datas[indx];
    val[role] = value;
    emit dataChanged(index(indx, 0), index(indx, 0), QVector<int>() << roles.key(role));
}

void VariantModel::move(int from, int to, int n) {
    if (from >= m_count || from < 0 || to < 0 || to >= m_count
        || !beginMoveRows(QModelIndex(), from, from + n - 1, QModelIndex(), to > from ? to + 1 : to))
        return;
    if (n > 1 && from + n < to && to + n < m_count) {
        qDebug() << "n > 1";
        for (int i = 0; i < n; i++)
            datas.move(from + i, to + i);
    } else
        datas.move(from, to);
    endMoveRows();
}

QList<QByteArray> VariantModel::getModelRoles() const {
    return modelRoles;
}

void VariantModel::setModelRoles(const QList<QByteArray> &value) {
    modelRoles = value;

    int roleCount = Qt::UserRole;
    for (auto &&role : modelRoles)
        roles[++roleCount] = role;
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
    return datas;
}

void VariantModel::clear() {
    beginResetModel();
    datas.clear();
    setCount(0);
    endResetModel();
}
