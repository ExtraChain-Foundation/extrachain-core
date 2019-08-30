#include "extracoin/headers/ui/etutils.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfoList>
#include <QUrl>

EtUtils::EtUtils(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_ANDROID)
    QAndroidJniObject vibroString = QAndroidJniObject::fromString("vibrator");
    QAndroidJniObject activity = QAndroidJniObject::callStaticObjectMethod(
        "org/qtproject/qt5/android/QtNative", "activity", "()Landroid/app/Activity;");
    QAndroidJniObject appctx =
        activity.callObjectMethod("getApplicationContext", "()Landroid/content/Context;");
    vibratorService = appctx.callObjectMethod("getSystemService",
                                              "(Ljava/lang/String;)Ljava/lang/Object;",
                                              vibroString.object<jstring>());
#endif

    auto networkState = [this](bool isOnline) {
        bool isActive = false;

        QList<QNetworkConfiguration> activeConfigs =
            ncm.allConfigurations(QNetworkConfiguration::Active);
        if (activeConfigs.count() > 0)
            isActive = true;

        qDebug() << "[UI Network Status]" << (isOnline && isActive);
        setNetworkActive(isOnline && isActive);
    };

    networkState(true);
    QObject::connect(&ncm, &QNetworkConfigurationManager::onlineStateChanged, networkState);
}

QString EtUtils::keccak(const QString &value)
{
    return QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Keccak_256)
        .toHex();
}

bool EtUtils::fileExists(const QString &filePath)
{
    return QFile::exists(QUrl(filePath).toLocalFile());
}

bool EtUtils::isImage(const QString &filePath)
{
    return false;
    //    QImageReader image(QUrl(filePath).toLocalFile());
    //    return image.canRead();
}

#if defined(Q_OS_ANDROID)
void EtUtils::vibrate(int milliseconds)
{
    if (vibratorService.isValid())
    {
        jlong ms = milliseconds;
        jboolean hasvibro = vibratorService.callMethod<jboolean>("hasVibrator", "()Z");
        vibratorService.callMethod<void>("vibrate", "(J)V", ms);
    }
    else
    {
        qDebug() << "[Android] No vibrator service available";
    }
}
#else
void EtUtils::vibrate(int milliseconds)
{
    Q_UNUSED(milliseconds);
}
#endif

void EtUtils::wipe()
{
    EtUtils::removeDataFiles();
}

bool EtUtils::firstIdCreated()
{
    return QDir("data/1").exists();
}

QString EtUtils::toFilePath(const QString &path) // from absolute path to file:///
{
    return QUrl::fromLocalFile(path).toString();
}

QByteArray EtUtils::serialize(QList<QByteArray> list)
{
    for (auto &&value : list)
    {
        if (value.indexOf("'") != -1)
            value.replace("'", "\\'");
    }

    QByteArray serialized = "'" + list.join("','") + "'";
    return serialized;
}

QString EtUtils::serializeStr(QStringList list)
{
    for (auto &&value : list)
    {
        if (value.indexOf("'") != -1)
            value.replace("'", "\\'");
    }

    QString serialized = "'" + list.join("','") + "'";
    return serialized;
}

QList<QByteArray> EtUtils::deserialize(const QString &serialized)
{
    QStringList deserializedStr = serialized.mid(1, serialized.length() - 2).split("','");
    QList<QByteArray> deserialized;

    for (auto &&value : deserializedStr)
    {
        if (value.indexOf("'") != -1)
            value.replace("\\'", "'");
        deserialized << value.toUtf8();
    }

    return deserialized;
}

QStringList EtUtils::deserializeStr(const QString &serialized)
{
    QStringList deserialized = serialized.mid(1, serialized.length() - 2).split("','");

    for (auto &&value : deserialized)
    {
        if (value.indexOf("'") != -1)
            value.replace("'", "\\'");
    }

    return deserialized;
}

void EtUtils::removeDataFiles()
{
    QDir("data").removeRecursively();
    QDir("../DATA").removeRecursively();
    QDir removeDir("blockchain/index/actors/0");
    QFileInfoList fileList = removeDir.entryInfoList();
    for (auto &el : fileList)
        if ((el.fileName() != "0") && (el.fileName() != ".") && (el.fileName() != ".."))
            QFile(el.filePath()).remove();
    removeDir = "blockchain/index/blocks/0";
    fileList = removeDir.entryInfoList();
    for (auto &el : fileList)
        if ((el.fileName() != "0") && (el.fileName() != ".") && (el.fileName() != ".."))
            QFile(el.filePath()).remove();
    QDir("data").removeRecursively();
    QDir("DATA").removeRecursively();
//    QDir("keystore").removeRecursively();
    removeDir = "keystore/personal";
    fileList = removeDir.entryInfoList();
    for (auto &el : fileList)
        if ((el.fileName() != "0") && (el.fileName() != ".") && (el.fileName() != ".."))
            QFile(el.filePath()).remove();
    QDir("tmp").removeRecursively();
}

bool EtUtils::autoWipe(int wipe)
{
    QSettings settings;
    QString num = QString::number(wipe);

    if (!settings.value("wipes/" + num).isValid())
        settings.setValue("wipes/" + num, false);

    if (!settings.value("wipes/" + num).toBool())
    {
        qDebug() << "Wipe all";
        EtUtils::removeDataFiles();
        settings.setValue("wipes/" + num, true);
        return true;
    }

    return false;
}

bool EtUtils::networkActive() const
{
    return m_networkActive;
}

void EtUtils::setNetworkActive(bool networkActive)
{
    if (m_networkActive == networkActive)
        return;

    m_networkActive = networkActive;
    emit networkActiveChanged(m_networkActive);
}
