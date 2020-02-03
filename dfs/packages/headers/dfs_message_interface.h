#ifndef DFS_MESSAGE_INTERFACE_H
#define DFS_MESSAGE_INTERFACE_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <iostream>
#include <QObject>
#include "utils/utils.h"

namespace DistFileSystem {

const long long dataSize = 800; // bytes
const short fieldsSize = 8;     // bytes for size
const QByteArray stateDelimetr = "|";
}

#endif // DFS_MESSAGE_INTERFACE_H
