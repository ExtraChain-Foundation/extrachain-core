#ifndef INSERTERFILES_H
#define INSERTERFILES_H

#include "managers/extrachain_node.h"
#include <QObject>
#include <QThread>

class InserterFiles : public QThread {
    Q_OBJECT

    QStringList fList;
    ExtraChainNode &node;

public:
    InserterFiles(ExtraChainNode &xNode, const QStringList &files, QObject *parent = nullptr);

protected:
    void run() override;

signals:
    void resultAddFile(const QString &result, const QString &fileName);
};

#endif // INSERTERFILES_H
