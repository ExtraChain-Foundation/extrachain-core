#ifndef LOGSMANAGER_H
#define LOGSMANAGER_H

#include <QString>
#include <QObject>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <iostream>
#ifdef ETALONIUM_CLIENT
#include "ui/model/abstractmodel.h"
#endif

class LogsManager : public QObject
{
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
    static void logPrint(const std::string& log);

    static bool toConsole;
    static bool toFile;
    static bool toQml;
    static bool antiFilter;
    static bool debugLogs;
#ifdef ETALONIUM_CLIENT
    static AbstractModel logs;
#endif

    static QStringList filesFilter;
    static void setFilesFilter(const QStringList& value);
    static void setAntiFilter(bool value);

signals:
    void makeLogSignal(const QString& file, int line, const QString& function, const QString& msg);

public: // slots:
    static void makeLog(const QString& file, int line, const QString& function, const QString& msg);
};

#endif // LOGSMANAGER_H
