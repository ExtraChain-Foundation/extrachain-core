#include "utils/autologinhash.h"

#include <QDebug>
#include <QFile>

bool AutologinHash::load() {
    if (!QFile::exists(".auth_hash"))
        return false;

    QFile file(".auth_hash");
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "[Autologin Hash] Can't read auth hash file";
        return false;
    }
    m_hash = file.read(128);
    file.close();
    return m_hash.size() == 128;
}

void AutologinHash::save(const std::string& hash) {
    auto hashBytes = QByteArray::fromStdString(hash);
    QFile file(".auth_hash");
    if (!file.open(QFile::WriteOnly) && file.write(hashBytes) > 0) {
        qFatal("[Autologin Hash] Can't write to auth hash file");
        return;
    }
    file.write(hashBytes);
    file.close();

    m_hash = hash;
}

const std::string& AutologinHash::hash() const {
    return m_hash;
}

bool AutologinHash::isAvailable() {
    AutologinHash hash;
    return hash.load();
}
