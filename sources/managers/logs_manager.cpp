/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include "managers/logs_manager.h"

#include <QMutex>

#ifdef Q_OS_ANDROID
#include <android/log.h>
#endif

bool LogsManager::toConsole = true;
bool LogsManager::toFile = true;
bool LogsManager::toQml =
#ifdef QT_DEBUG
    true;
#else
    true;
#endif

#ifdef ECLIENT
AbstractModel LogsManager::logs = AbstractModel(nullptr, { "text", "date", "file", "line", "func" });
#endif

QStringList LogsManager::filesFilter;
bool LogsManager::antiFilter = false;
bool LogsManager::debugLogs = false;

LogsManager::LogsManager()
{
    connect(this, &LogsManager::makeLogSignal, this, &LogsManager::makeLog);
}

void LogsManager::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // static LogsManager logsManager;
    // emit logsManager.makeLogSignal(context.file, context.line, context.function, msg);
    switch (type)
    {
    case QtInfoMsg:
        logPrint(msg.toStdString());
        break;
    default:
        if (debugLogs)
            makeLog(context.file, context.line, context.function, msg);
        break;
    }
}

void LogsManager::makeLog(const QString& file, int line, const QString& function, const QString& msg)
{
    static QFile logFile("logs/extrachain" + QDateTime::currentDateTime().toString("-MM-dd-hh.mm.ss")
                         + ".log");

    if (LogsManager::toFile && !logFile.isOpen())
        logFile.open(QFile::Append | QFile::Text);

    QString message = msg;
    QDateTime currentDateTime = QDateTime::currentDateTime();

#ifdef QT_DEBUG
    // TODO: to std::string
    QString fileName = file;
    if (fileName.isEmpty())
        fileName = "global";

#ifdef Q_OS_WIN
    fileName = fileName.right(fileName.size() - fileName.lastIndexOf("\\") - 1);
#else
    fileName = fileName.right(fileName.size() - fileName.lastIndexOf("/") - 1);
#endif
#endif

#ifdef QT_DEBUG
    bool isPrint = !filesFilter.length();

    if (!isPrint)
    {
        for (auto&& file : filesFilter)
        {
            if (antiFilter ? !fileName.contains(file) : fileName.contains(file))
            {
                isPrint = true;
                break;
            }
        }
    }

    if (!isPrint)
        return;

    QString fileNameQrc, lineRow;
    if (fileName.right(3) == "qml")
    {
#ifdef Q_OS_WIN
        fileNameQrc = QString("%1:%2").arg(fileName).arg(line);
#else
        fileNameQrc = QString("qrc:/%1:%2").arg(fileName).arg(line);
#endif

        if (message.left(fileNameQrc.length()) == fileNameQrc)
        {
            lineRow = message.mid(fileNameQrc.length(),
                                  message.length()
                                      - (message.length() - message.indexOf(":", fileNameQrc.length() + 1))
                                      - fileNameQrc.length());

            fileNameQrc = fileNameQrc + lineRow;

            message = "Warning: " + message.right(message.length() - fileNameQrc.length() - 2);
        }
    }

    QString fileNameStd;
    if (fileName != "global")
        fileNameStd =
#ifdef ECLIENT
            "file:/" +
#endif
            fileName;
    else
        fileNameStd = "global";
#endif

    const QString logStr = currentDateTime.toString("hh:mm:ss ")
#ifdef QT_DEBUG
        + "["
        + (fileNameQrc.length() ? fileNameQrc
                                : fileNameStd + (fileNameStd == "global" ? "" : ":" + QString::number(line)))
        + "] "
#endif
        + message;

    if (LogsManager::toConsole)
    {
#ifdef QT_DEBUG
        if (isPrint)
#endif
            logPrint(logStr.toStdString());
    }

#ifdef ECLIENT
    if (LogsManager::toQml)
    {
        static QMutex mutex;
        mutex.lock();
        logs.append({ { "text", msg },
                      { "date", currentDateTime.toMSecsSinceEpoch() }
#ifdef QT_DEBUG
                      ,
                      { "file", fileName },
                      { "line", line },
                      { "func", function }
#endif
        });
        mutex.unlock();
    }
#endif

    if (LogsManager::toFile && logFile.isWritable())
    {
        static QMutex mutex;
        mutex.lock();
        logFile.write(QString("%1 %2\n").arg(currentDateTime.toString("yyyy-MM-dd"), logStr).toUtf8());
        logFile.flush();
        mutex.unlock();
    }
}

void LogsManager::on()
{
    LogsManager::toConsole = true;
    LogsManager::toFile = true;
    LogsManager::toQml = true;
}

void LogsManager::off()
{
    LogsManager::toConsole = false;
    LogsManager::toFile = false;
    LogsManager::toQml = false;
}

void LogsManager::onConsole()
{
    LogsManager::toConsole = true;
}

void LogsManager::offConsole()
{
    LogsManager::toConsole = false;
}

void LogsManager::onFile()
{
    QDir().mkdir("logs");
    LogsManager::toFile = true;
}

void LogsManager::offFile()
{
    LogsManager::toFile = false;
}

void LogsManager::onQml()
{
    LogsManager::toQml = true;
}

void LogsManager::offQml()
{
    LogsManager::toQml = false;
}

void LogsManager::etHandler()
{
    std::ios_base::sync_with_stdio(false);

    QDir().mkdir("logs");
    qInstallMessageHandler(LogsManager::messageHandler);
}

void LogsManager::qtHandler()
{
    qInstallMessageHandler(nullptr);
}

void LogsManager::emptyHandler()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
        Q_UNUSED(type)
        Q_UNUSED(context)
        Q_UNUSED(msg)
    });
}

void LogsManager::logPrint(const std::string& log)
{
#ifdef Q_OS_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "ExtraChain", "%s", log.c_str());
#else
    std::cout << log << std::endl;
#endif
}

void LogsManager::setAntiFilter(bool value)
{
    antiFilter = value;
}

void LogsManager::setFilesFilter(const QStringList& value)
{
    filesFilter = value;
}
