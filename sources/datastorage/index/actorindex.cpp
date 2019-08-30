#include "datastorage/index/actorindex.h"

ActorIndex::ActorIndex()
    : FileIndex(/*DataStorage::BLOCKCHAIN_INDEX + '/' +*/ DataStorage::ACTOR_INDEX_FOLDER_NAME)
{
}

ActorIndex::ActorIndex(QString folderName)
    : FileIndex(folderName)
{
}

Actor<KeyPublic> ActorIndex::getActor(const BigNumber &id) const
{

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty())
    {
        return Actor<KeyPublic>(serializedActor);
    }
    qDebug() << "There no actor with id:" << id;
    return Actor<KeyPublic>();
}

bool ActorIndex::validateBlock(const Block &block) const
{
    Actor<KeyPublic> actor = this->getActor(block.getApprover());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate block" << block.getIndex() << ": There no actor"
                   << block.getApprover() << " in local storage";
        return false;
    }
    return block.verify(actor);
}

bool ActorIndex::validateTx(const Transaction &tx) const
{
    Actor<KeyPublic> actor = this->getActor(tx.getApprover());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate tx" << tx.getHash() << ": There no actor"
                   << tx.getApprover() << " in local storage";
        return false;
    }
    return tx.verify(actor);
}

// todo: look closely at this method!
void ActorIndex::validatePrivateActor(Actor<KeyPrivate> *actor)
{
    if (actor == nullptr)
    {
        qCritical() << "Null pointer";
        return;
    }

    KeyPrivate *prKey = actor->getKey();
    BigNumber last = getLastSavedId();

    for (BigNumber i = getFirstSavedId(); i < last; ++i)
    {
        if (getActor(i).getKey()->extractPublicKey() == prKey->extractPublicKey())
        {
            qDebug() << "Error: Created actor is not unique";
            return;
        }
    }

    emit PrivateActorIsVerified(*actor);
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor)
{
    //    qDebug() << "adfklsfkl;adskl;afsdl;afsdl;";
    switch (addActor(actor))
    {
    case 0:
        qDebug() << QString("New actor [%1] is successfully saved").arg(actor.toString());
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << QString("New actor [%1] can't be added: it is already in storage")
                        .arg(actor.toString());
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning() << QString("Error: new actor [%1] is not saved").arg(actor.toString());
        break;
    default:
        qWarning() << "Error: unexpected return type";
    }
}

void ActorIndex::handleNewActorCheck(Actor<KeyPublic> actor)
{
    if (getActor(actor.getId()).isEmpty())
    {
        handleNewActor(actor);
        emit ActorIsMissing(actor);
    }
}

bool ActorIndex::actorExist(BigNumber actorId)
{
    if (getById(actorId) == QByteArray())
        return false;
    return true;
}

int ActorIndex::addActor(const Actor<KeyPublic> &actor)
{
    int result = this->add(actor.getId(), actor.serialize());
    qDebug() << actor.getId().serialize() << " =~= " << lastSavedId.serialize();
    if (actor.getId() + 1 == lastSavedId || lastSavedId == BigNumber(1))
        emit actorIndexUpdated();
    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        qDebug() << "ActorIndex: actor - " << actor.getId() << " was added "
                 << "lsd: " << lastSavedId;
        // todo: Event should be emited only on CREATING new actors, not on RECEIVING new
        // one's make methods:
        // * addActor -> add actor to storage
        // * addNewActor -> add actor to storage and emit event NewActor
        if (actor.getId() > BigNumber(0))
        {
            //            ++lastSavedId;
            emit NewActor(actor);
        }
        if (actor.getAccount())
        {
            qDebug() << "emit signal for init dfs for user" << actor.getId().toByteArray();
            emit initDfs(actor.getId());
        }
    }
    return result;
}
