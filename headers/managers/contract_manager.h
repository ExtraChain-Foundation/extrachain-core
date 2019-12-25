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
