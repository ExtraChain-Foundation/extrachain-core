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

#include "dfs/managers/headers/sender.h"

Sender::Sender(QObject *parent)
    : QObject(parent)
{
    // connect(this, &Sender::resendFragments, this, &Sender::resendFragmentsSlot);
}

void Sender::setNetManager(DFSNetManager *value)
{
    NetManager = value;
}

void Sender::sendFragments(QString path, DfsStruct::Type type, QByteArray frag, const SocketPair &receiver)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
    {
        DistFileSystem::TitleMessage title;
        title.filePath = path;
        title.f_type = type;
        title.calcHash();
        std::vector<long long> fragsID;
        QByteArrayList frags = frag.split(' ');

        for (QByteArray b : frags)
        {
            if (b.indexOf(":") == -1)
            {
                fragsID.push_back(b.toLongLong());
            }
            else
            {
                QByteArrayList bs = b.split(':');
                unsigned long s = bs[0].toULong();
                unsigned long e = bs[1].toULong();
                for (unsigned long i = s; i <= e; i++)
                {
                    fragsID.push_back(static_cast<long long>(i));
                }
            }
            // fragsID.push_back(b.toLongLong());
        }
        //        int p = 0;
        for (unsigned int i = 0; i < fragsID.size(); i++)
        {
            file.seek(fragsID[i] * data_offset);
            QByteArray data = file.read(data_offset);
            DistFileSystem::DfsMessage pck; // package for send
            pck.dataHash = title.dataHash;
            pck.pckgNumber = fragsID[i];
            pck.data = data;
            pck.path = path;
            //            if (p >= 20)
            //            {
            //                p = 0;
            //                QThread::currentThread()->msleep(50);
            //            }
            //            else
            //            {
            //                p++;
            //            }
            sendDfsMessage(pck, Messages::DFSMessage::fileDataMessage, receiver);
        }
    }
}

void Sender::process()
{
}

void Sender::sendFile(const QString &filePath, const DfsStruct::Type &type, const SocketPair &receiver)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    // create title_message
    // unsigned long pckgN = 0; // package number
    DistFileSystem::TitleMessage title;
    title.filePath = filePath;
    title.f_type = type;
    title.calcHash();
    if (title.isEmpty())
    {
        qDebug() << "empty title";
        return;
    }

    qDebug() << "DataHash from title:" << title.dataHash;
    sendDfsMessage(title, Messages::DFSMessage::titleMessage, receiver);
    file.close();
}
