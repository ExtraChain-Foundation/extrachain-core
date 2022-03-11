#include "datastorage/blockchain/blockchain.h"

Blockchain2::Blockchain2(int mode, AccountController *AccountController, ActorIndex *ActorIndex) {
    this->accountController = AccountController;
    this->actorIndex = ActorIndex;
}

Blockchain2::~Blockchain2() {
    //
}

int Blockchain2::addBlock(const Block &block, bool isGenesis) {
    int resultCode = Errors::SUCCESS;
    sfs::path path = buildBlockFilePath(block);
    if (sfs::exists(path)) {
        qDebug() << "Can't save the file" << path.c_str() << "(File already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }
    if (isLimitReached()) {
        // TODO!!!
    }

    return writeBlock(path, block, isGenesis);
}

int Blockchain2::writeBlock(sfs::path path, const Block &block, bool isGenesis) {
    if (block.getType() == Config::GENESIS_BLOCK_TYPE) {
        const GenesisBlock &genblock = dynamic_cast<const GenesisBlock &>(block);
        writeGenBlock(path, genblock);
    } else {
        writeOrdBlock(path, block);
    }
}

int Blockchain2::writeGenBlock(std::filesystem::path path, const GenesisBlock &block) {
    //
}

int Blockchain2::writeOrdBlock(std::filesystem::path path, const Block &block) {
    //
}

std::string Blockchain2::buildBlockFilePath(const Block &block) {
    BigNumber section = block.getIndex() / sectionSize;
    sfs::path path(DataStorage::BLOCKCHAIN_FOLDER + "/" + section.toStdString());
    if (!sfs::is_directory(path) || !sfs::exists(path)) {
        sfs::create_directory(path);
    }
    return path.generic_string() + block.getIndex().toStdString();
}

bool Blockchain2::isLimitReached() {
    if (!sizeLimitBytes.isEmpty()) {
        //
    }
    return false;
}
