/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <QObject>
#include <QtNetwork/QNetworkInformation>

class NetworkStatus : public QObject {
    Q_OBJECT

public:
    enum class Status {
        Unknown,
        Online,
        Offline,
        Local
    };
    Q_ENUM(Status)

    explicit NetworkStatus(QObject* parent = nullptr);
    Status status();

private slots:
    void onReachabilityChanged(QNetworkInformation::Reachability reachability);

signals:
    void statusChanged(NetworkStatus::Status status);

private:
    void   setNetworkStatus(Status status);
    Status m_networkStatus = Status::Offline;
};
