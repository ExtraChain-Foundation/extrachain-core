#ifndef TEST_H
#define TEST_H

#include "dfs/controls/headers/dfs.h"
#include "QRandomGenerator"
//class Test : public QObject
//{
//    Q_OBJECT
//private:
//    QStringList pathImageList;

//public:
//    Test(Dfs *dfs, QObject *parent = nullptr)
//        : QObject(parent)
//    {
//        qDebug() << "TEST" << connect(this, &Test::testSaveNewData, dfs, &Dfs::savedNewData);
//        qDebug() << "TEST" << connect(dfs, &Dfs::sendMessage, this, &Test::testSendData);

//        qDebug() << "TEST" << connect(dfs, &Dfs::usersChanges, this, &Test::testUserChanges)
//                 << connect(this, &Test::testRecieverData, dfs, &Dfs::recieve);
//    }
//    void run()
//    {
//        QString path = "../test1.jpeg";
//        emit testSaveNewData(path, based_dfs_struct::images, based_dfs_struct::SubType::ievent,
//                             based_dfs_struct::NEW);
//        path = "../test2.jpeg";
//        emit testSaveNewData(path, based_dfs_struct::images, based_dfs_struct::SubType::ievent,
//                             based_dfs_struct::NEW);
//        //        path = "";
//        //        emit testSaveNewData(path, based_dfs_struct::images,
//        //        based_dfs_struct::SubType::ievent,
//        //                             based_dfs_struct::NEW);
//        //        path = "";
//        //        emit testSaveNewData(path, based_dfs_struct::images,
//        //        based_dfs_struct::SubType::ievent,
//        //                             based_dfs_struct::NEW);
//        //        path = "";
//        //        emit testSaveNewData(path, based_dfs_struct::images,
//        //        based_dfs_struct::SubType::ievent,
//        //                             based_dfs_struct::NEW);
//    }
//signals:
//    void testSaveNewData(const QString &path, const based_dfs_struct::Type &type,
//                         const based_dfs_struct::SubType &subType,
//                         const based_dfs_struct::Status &status);
//    void testRecieverData(Messages::DfsMessage msg);
//public slots:
//    void testSendData(const Messages::DfsMessage &msg)
//    {
//        QFile file("../testSendData.txt");
//        file.open(QIODevice::WriteOnly | QIODevice::Append);
//        file.write("msg.getProtocol()::" + msg.getProtocol() + "\n");
//        file.write("msg.getFilePath::" + msg.getFilePath().toUtf8() + "\n");
//        file.write("msg.getSize()::" + QByteArray::number(msg.getData().size()) + "\n");
//        //        if ( != based_dfs_struct::images)
//        file.write("msg.getData()::" + msg.getData() + "\n");
//        file.close();
//        QByteArray name =
//            Serialization::deserialize(msg.getFilePath().toUtf8() + "/", "/").at(4);
//        Messages::DfsMessage testMsg(msg.getData(), msg.getSize(),
//                                     "../testRecieveData" + name, 1, 5);
//        emit testRecieverData(testMsg);
//    }
//    void testUserChanges(QByteArray data, based_dfs_struct::Type type, BigNumber actorId)
//    {
//        QFile file("../testUserChanges.txt");
//        file.open(QIODevice::WriteOnly | QIODevice::Append);
//        if (type != based_dfs_struct::images)
//            file.write("data::" + data + '\n');
//        file.write("type::" + based_dfs_struct::toByteArray(type) + '\n');
//        file.write("actroId::" + actorId.toByteArray() + "\n");
//        file.close();
//    }
//};

#endif // TEST_H
