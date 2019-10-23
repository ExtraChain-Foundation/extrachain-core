#ifndef PACKAGE_RESOLVER_H
#define PACKAGE_RESOLVER_H

#include "dfs/managers/headers/dfsindex.h"

#include "dfs/packages/headers/dfs_message_interface.h"
#include "dfs/packages/headers/all.h"

class Sender : public QObject
{
    Q_OBJECT

    AccountController *account;

public:
    Sender(AccountController *account)
    {
        this->account = account;
    }

    void sendFile(const QString &filePath, QString peerAdrress)
    {
        QFile file(filePath);
        file.open(QIODevice::ReadOnly);
        int bytes_count = 0;
        long long _file_size = file.size();
        if (_file_size < 513)
        {
            QByteArray data = file.readAll();
            Messages::DfsMessage msg(data, _file_size, filePath, 0, 1);
            if (peerAdrress.isEmpty())
                emit sendData(msg);
            else
                emit sendToUser(msg, peerAdrress);
            this->thread()->sleep(1);
        }
        else
        {
            const int _data_offset = 512;

            int g = _file_size / _data_offset;
            g += ((_file_size % _data_offset) != 0) ? 1 : 0;
            long long pos = 0;
            while ((pos + _data_offset) < _file_size)
            {
                char *ch = new char[_data_offset];
                file.read(ch, _data_offset);
                pos += _data_offset;
                Messages::DfsMessage msg(QByteArray(ch, _data_offset), _file_size, filePath, bytes_count, g);
                if (peerAdrress.isEmpty())
                    emit sendData(msg);
                else
                    emit sendToUser(msg, peerAdrress);
                delete[] ch;
                bytes_count++;
                this->thread()->sleep(1);
            }
            int _last_offset = _file_size - pos;
            char *ch = new char[_last_offset];
            file.read(ch, _last_offset);
            Messages::DfsMessage msg(QByteArray(ch, _last_offset), _file_size, filePath, bytes_count, g);
            //        msg.setNeedsByteCount(bytes_count);
            if (peerAdrress.isEmpty())
                emit sendData(msg);
            else
                emit sendToUser(msg, peerAdrress);
            this->thread()->sleep(1);
            delete[] ch;
        }
        file.close();
    }

signals:

    void finished();

    void sendMsg(const QByteArray &data, const QByteArray &msgType);

    void sendData(const Messages::DfsMessage &msg);

    void sendToUser(const Messages::DfsMessage &msg, const QString &peerAddress);
public slots:
    void process()
    {
    }
};

class DFSResolver : public QObject
{
    Q_OBJECT

private:
    DFSResolver(QObject *parent = nullptr);
    ~DFSResolver();

    bool active = false;
    QByteArray msg;
    QByteArray hash;

public:
    DFSResolver(AccountController *account);

    void sendFile(const QString &fileName);
    void validate();
    bool isActive() const;
    void recieveMsg(const QByteArray &msgS, const SocketPair &receiver);
signals:

    void finished();

    void sendMsg(const QByteArray &data, const QByteArray &msgType);

public slots:
    void process()
    {
    }
};
#endif
