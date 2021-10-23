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

#ifndef LOGSMANAGER_H
#define LOGSMANAGER_H

#include "extrachain_global.h"
#include "utils/variant_model.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>

class EXTRACHAIN_EXPORT LogsManager : public QObject {
    Q_OBJECT

public:
    LogsManager();

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    static void on();
    static void off();
    static void onConsole();
    static void offConsole();
    static void onFile();
    static void offFile();
    static void onQml();
    static void offQml();
    static void etHandler();
    static void qtHandler();
    static void emptyHandler();
    static void print(const std::string& log);
    static void setDebugLogs(bool debugLogs);

    static bool toConsole;
    static bool toFile;
    static bool toModel;
    static bool antiFilter;
    static bool debugLogs;
    static VariantModel logs;

    static QStringList filesFilter;
    static void setFilesFilter(const QStringList& value);
    static void setAntiFilter(bool value);

signals:
    void makeLogSignal(const QString& file, int line, const QString& function, const QString& msg);

public: // slots:
    static void makeLog(const QString& file, int line, const QString& function, const QString& msg);

private:
    static QString normalizeFileName(const QString& file);
};

struct UnicodedStream : QTextStream {
    using QTextStream::QTextStream;

    template <typename T>
    UnicodedStream& operator<<(T const& t) {
        return static_cast<UnicodedStream&>(static_cast<QTextStream&>(*this) << t);
    }

    UnicodedStream& operator<<(char const* ptr) {
        return static_cast<UnicodedStream&>(*this << QString(ptr));
    }
};

#endif // LOGSMANAGER_H
