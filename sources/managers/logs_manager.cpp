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

#ifdef _WIN32
    #include <Windows.h>
#endif

#include "managers/logs_manager.h"

#include <fmt/core.h>

#include <QJsonObject>
#include <QMutex>

#ifdef Q_OS_ANDROID
    #include <android/log.h>
#endif

#ifdef QT_DEBUG
    #define LOG_FILENAME
#endif

bool LogsManager::toConsole = true;
bool LogsManager::toFile = false;
bool LogsManager::toModel =
#ifdef LOG_FILENAME
    true;
#else
    true;
#endif

VariantModel LogsManager::logs = VariantModel(nullptr, { "text", "date", "file", "line", "func" });
QStringList LogsManager::filesFilter;
bool LogsManager::antiFilter = false;
bool LogsManager::debugLogs = false;

LogsManager::LogsManager() {
    // connect(this, &LogsManager::makeLogSignal, this, &LogsManager::makeLog);
}

void LogsManager::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    // static LogsManager logsManager;
    // emit logsManager.makeLogSignal(context.file, context.line, context.function, msg);
    switch (type) {
    case QtInfoMsg:
        makeLog(context.file, context.line, context.function, msg);
        break;
    case QtCriticalMsg:
        makeLog(context.file, context.line, context.function, "[Critical] " + msg);
        break;
    case QtFatalMsg: {
        makeLog(context.file, context.line, context.function, "[Fatal Error] " + msg);

        QFile file("logs/extrachain-fatal.log");
        if (file.open(QFile::Append)) {
            QJsonObject json;
            json["time"] = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss ap");
#ifdef LOG_FILENAME
            json["file"] = normalizeFileName(context.file);
            json["line"] = QString::number(context.line);
            json["function"] = QString(context.function);
#endif
            json["message"] = msg;
            file.write(QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n");
            file.close();
        }
        break;
    }
    default:
        if (debugLogs)
            makeLog(context.file, context.line, context.function, msg);
        break;
    }
}

void LogsManager::makeLog(const QString& file, int line, const QString& function, const QString& msg) {
    Q_UNUSED(file)
    Q_UNUSED(line)
    Q_UNUSED(function)
    static QFile logFile("logs/extrachain" + QDateTime::currentDateTime().toString("-MM-dd-hh.mm.ss")
                         + ".log");

    if (LogsManager::toFile && !logFile.isOpen())
        logFile.open(QFile::Append | QFile::Text);

    QString message = msg;
    QDateTime currentDateTime = QDateTime::currentDateTime();

#ifdef LOG_FILENAME
    // TODO: to std::string
    QString fileName = normalizeFileName(file);
    bool isPrint = !filesFilter.length();

    if (!isPrint) {
        for (auto&& file : filesFilter) {
            if (antiFilter ? !fileName.contains(file) : fileName.contains(file)) {
                isPrint = true;
                break;
            }
        }
    }

    if (!isPrint)
        return;

    QString fileNameQrc, lineRow;
    if (fileName.right(3) == "qml") {
    #ifdef Q_OS_WIN
        fileNameQrc = QString("%1:%2").arg(fileName).arg(line);
    #else
        fileNameQrc = QString("qrc:/%1:%2").arg(fileName).arg(line);
    #endif

        if (message.left(fileNameQrc.length()) == fileNameQrc) {
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
        fileNameStd = "file:/" + fileName;
    else
        fileNameStd = "global";
#endif

    const QString logStr = currentDateTime.toString("hh:mm:ss ")
#ifdef LOG_FILENAME
        + "["
        + (fileNameQrc.length() ? fileNameQrc
                                : fileNameStd + (fileNameStd == "global" ? "" : ":" + QString::number(line)))
        + "] "
#endif
        + message;

    if (LogsManager::toConsole) {
#ifdef LOG_FILENAME
        if (isPrint)
#endif
            print(logStr.toStdString());
    }

    if (LogsManager::toModel) {
        static QMutex mutex;
        mutex.lock();
        logs.append({ { "text", msg },
                      { "date", currentDateTime.toMSecsSinceEpoch() }
#ifdef LOG_FILENAME
                      ,
                      { "file", fileName },
                      { "line", line },
                      { "func", function }
#endif
        });
        mutex.unlock();
    }

    if (LogsManager::toFile && logFile.isWritable()) {
        static QMutex mutex;
        mutex.lock();
        logFile.write(QString("%1 %2\n").arg(currentDateTime.toString("yyyy-MM-dd"), logStr).toUtf8());
        logFile.flush();
        mutex.unlock();
    }
}

QString LogsManager::normalizeFileName(const QString& file) {
#ifdef LOG_FILENAME
    // TODO: to std::string
    QString fileName = file;
    if (fileName.isEmpty())
        fileName = "global";

    #if defined(Q_OS_WIN) && !defined(__GNUC__)
    return fileName.right(fileName.size() - fileName.lastIndexOf("\\") - 1);
    #else
    return fileName.right(fileName.size() - fileName.lastIndexOf("/") - 1);
    #endif
#else
    Q_UNUSED(file)
    return "";
#endif
}

void LogsManager::on() {
    LogsManager::toConsole = true;
    LogsManager::toFile = true;
    LogsManager::toModel = true;
}

void LogsManager::off() {
    LogsManager::toConsole = false;
    LogsManager::toFile = false;
    LogsManager::toModel = false;
}

void LogsManager::onConsole() {
    LogsManager::toConsole = true;
}

void LogsManager::offConsole() {
    LogsManager::toConsole = false;
}

void LogsManager::onFile() {
    QDir().mkpath("logs");
    LogsManager::toFile = true;
}

void LogsManager::offFile() {
    LogsManager::toFile = false;
}

void LogsManager::onQml() {
    LogsManager::toModel = true;
}

void LogsManager::offQml() {
    LogsManager::toModel = false;
}

void LogsManager::etHandler() {
    std::ios_base::sync_with_stdio(false);
    qInstallMessageHandler(LogsManager::messageHandler);

#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void LogsManager::qtHandler() {
    qInstallMessageHandler(nullptr);
}

void LogsManager::emptyHandler() {
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
        Q_UNUSED(type)
        Q_UNUSED(context)
        Q_UNUSED(msg)
    });
}

void LogsManager::print(const std::string& log) {
#ifdef Q_OS_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "ExtraChain", "%s", log.c_str());
#else
    #if defined(Q_OS_WIN)
    if (IsDebuggerPresent()) {
        OutputDebugStringA(log.c_str());
        OutputDebugStringA("\n");
    }
    #endif
    fmt::print("{}\n", log);
    fflush(stdout);
#endif
}

void LogsManager::setDebugLogs(bool debugLogs) {
    LogsManager::debugLogs = debugLogs;
}

void LogsManager::setAntiFilter(bool value) {
    antiFilter = value;
}

void LogsManager::setFilesFilter(const QStringList& value) {
    filesFilter = value;
}
