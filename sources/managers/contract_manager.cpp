#include <QDir>

#include "managers/contract_manager.h"
#include "datastorage/contract.h"
#include "managers/account_controller.h"
#include "dfs/types/headers/dfstruct.h"

ContractManager::ContractManager(AccountController *accountController, Blockchain *blockchain)
{
    this->accountController = accountController;
    this->blockchain = blockchain;
}

void ContractManager::saveContract(const Contract &contract)
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + "/"
        + accountController->getMainActor()->getId().toString() + "/" + "CONTRACTS" + "/";
    qDebug() << path;
    QDir().mkpath(path);
    QDir contract_dir(path);

    QFile contract_file(path + QString(contract.getFileName()));
    contract_file.open(QIODevice::WriteOnly);
    contract_file.write(contract.serialize());
    contract_file.close();
}

void ContractManager::process()
{
}

void ContractManager::loadContracts()
{
    contractList.clear();
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + "/"
        + accountController->getMainActor()->getId().toString() + "/" + "CONTRACTS" + "/";
    QDir directory(path);
    if (!directory.exists())
        return;
    QStringList images = directory.entryList(QDir::Files);
    foreach (QString filename, images)
    {
        QFile open_file(path + filename);
        open_file.open(QIODevice::ReadOnly);
        contractList.push_back(Contract(open_file.readAll()));
    }
    for (auto temp : contractList)
    {
        qDebug() << temp.serialize();
    }
}

void ContractManager::loadContractsFrom()
{
    loadContracts();
    emit loadToUi(&contractList, accountController->getMainActor()->getId());
}

void ContractManager::createContract(const Contract &contract)
{
    contractList.append(contract);
    contractList.last().signByCustomer(*accountController->getMainActor());

    saveContract(contract);

    emit contractIsCreated(contractList.last());
    emit addContractToUi(contractList.last());
    qDebug() << "contract is added!";
}

void ContractManager::updateContract(const Contract &contract)
{
    saveContract(contract);

    emit contractIsCreated(contract);
    emit addContractToUi(contract);
    qDebug() << "contract is added!";
}

void ContractManager::addContractToManager(const Contract &contract)
{
    for (auto curr : contractList)
    {
        if (curr == contract)
            return;
    }
    if (!contract.getCustomer_sign().isEmpty() && !contract.getPerformer_sign().isEmpty()
        && contract.getFirst_transaction_hash().isEmpty()
        && contract.getCustomer() == accountController->getMainActor()->getId())
    {
        makeFirstContractTransaction(contract);
        return;
    }
    contractList.append(contract);
    saveContract(contract);
    loadContractsFrom();
    // update ui
}

void ContractManager::contractFromNetWork(const Contract &contract)
{
    qDebug() << accountController->getMainActor()->getId().serialize();
    if (contract.getCustomer() == accountController->getMainActor()->getId()
        || contract.getPerformer() == accountController->getMainActor()->getId())
    {
        addContractToManager(contract);
        // do some staff
    }
    //    if (contract.getCustomer() == accountController->getMainActor()->getId()) {
    //        if (contract.ver)
    //    }
}

void ContractManager::approveContractByPerformer(QByteArray hash)
{
    qDebug() << "ContractManager::approveContractByPerformer ";
    for (auto curr : contractList)
    {
        if (curr.getHash() == hash)
        {
            curr.signByPerformer(*accountController->getMainActor());
            saveContract(curr);
            contractIsCreated(curr);
            break;
        }
    }
}

void ContractManager::completeContractByCustomer(QByteArray hash)
{
    qDebug() << "ContractManager::completeContractByCustomer ";
    for (auto curr : contractList)
    {
        if (curr.getHash() == hash)
        {
            curr.completeContractByCustomer();
            saveContract(curr);
            contractIsCreated(curr);
            break;
        }
    }
}

void ContractManager::completeContractByPerformer(QByteArray hash)
{
    qDebug() << "ContractManager::completeContractByPerformer ";
    for (auto curr : contractList)
    {
        if (curr.getHash() == hash)
        {
            curr.completeContractByPerformer();
            saveContract(curr);
            contractIsCreated(curr);
            break;
        }
    }
}
