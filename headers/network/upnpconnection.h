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

#ifndef UPNPCONNECTION_H
#define UPNPCONNECTION_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QNetworkAddressEntry>
#include <QHostAddress>
#include <QTimer>
#include <QTextStream>
#include <QDateTime>
#include <QXmlStreamReader>
#include <QRandomGenerator>

class UPNPConnection : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Opened = 0,
        NotOpened,
        Closed
    };

private:
    QNetworkAddressEntry localAddress;
    int internalPort;
    QHostAddress externalAddress;
    int externalPort;
    QString info;
    QString pcol;

private:
    QHostAddress gateway;
    QUrl gatewayCtrlUrl;
    QString ctrlPort;
    QNetworkAccessManager *http_socket;
    QNetworkReply *http_reply;
    QUdpSocket *udp_socket;

private:
    QTimer *timer;
    State conn_state;
    int waitTime;

public:
    explicit UPNPConnection(QNetworkAddressEntry &local, QObject *parent = nullptr);
    ~UPNPConnection();

public:
    void makeTunnel(int internal, int external, QString protocol, QString text = "Tunnel ");
    void setTunnel();
    void removeTunnel();
    State getState() const;
    QHostAddress getExternalAddress() const;
    int getPort();

private:
    void getExternalIP();
    void checkTunnels();
    void extractExternalIP(QString message);
    void extractUPNPError(QString message);
    void postSOAP(QString action, QString message);
public slots:

private slots:
    void getUdp();
    void processReq(QNetworkReply *reply);
    void getHttp();
    void getHttpError(QNetworkReply::NetworkError err);
    void timeExpired();
signals:
    void success();
    void stageSucceded(QString stage);
    void udpResponse();
    void extAddressExtracted();
    void upnp_error(QString message);
};

#endif // UPNPCONNECTION_H
