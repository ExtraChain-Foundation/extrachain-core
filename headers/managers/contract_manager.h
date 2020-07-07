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

#ifndef CONTRACT_MANAGER_H
#define CONTRACT_MANAGER_H

#include <QObject>
#include "datastorage/contract.h"
#include <QDir>
#include "managers/account_controller.h"
#include "dfs/types/headers/dfstruct.h"

// class AccountController;
// class Blockchain;

// class ContractManager : public QObject
//{
//    Q_OBJECT
// private:
//    QList<Contract> contractList;

//    void saveContract(const Contract &contract);

// public slots:
//    void process();

//    void loadContracts();
//    void loadContractsFrom();
//    void createContract(const Contract &contract);
//    void updateContract(const Contract &contract);
//    void addContractToManager(const Contract &contract);
//    void contractFromNetWork(const Contract &contract);

//    void approveContractByPerformer(QByteArray hash);
//    void completeContractByCustomer(QByteArray hash);
//    void completeContractByPerformer(QByteArray hash);

// signals:
//    void finished();

//    void contractIsCreated(const Contract &contract);
//    void addContractToUi(const Contract &contract);
//    void loadToUi(QList<Contract> *, BigNumber currentId);

//    void makeFirstContractTransaction(const Contract &contract);
//    void makeFinalContractTransaction(const Contract &contract);
//};

#endif // CONTRACT_MANAGER_H
