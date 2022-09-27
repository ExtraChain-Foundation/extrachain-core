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

#include "managers/data_mining_manager.h"
#include "utils/exc_utils.h"

DataMiningManager::DataMiningManager(QObject *parent)
    : QObject(parent) {
}

void DataMiningManager::rootMerkleHash(std::vector<std::string> &listHashes,
                                              std::vector<MerkleDataBlocks> &branchesTree,
                                              const bool isHahsing,
                                              std::string &result) {
    if (listHashes.empty()) {
        qFatal("Root merkle hash: list is empty");
    };
    const auto splittedList = splitListIntoPair(listHashes, isHahsing);
    MerkleDataBlocks merkleBlocks;

    for (int index = 0; index < splittedList.size(); index++) {
        const auto pair = splittedList[index];

        if (pair.size() == 1) {
            merkleBlocks.push_back(pair[0]);
        } else {
            merkleBlocks.push_back(merkleFormula(pair[0], pair[1]));
        }
    }
    branchesTree.push_back(merkleBlocks);

    if (merkleBlocks.size() != 1) {
        rootMerkleHash(merkleBlocks, branchesTree, false, result);
    } else {
        result = branchesTree[branchesTree.size() - 1][0];
    }
}

std::string DataMiningManager::rootMerkleHash(std::string &data)
{
    std::string result;
    std::vector<MerkleDataBlocks> branches;
    std::vector<std::string> dataList;
    dataList.push_back(data);
    rootMerkleHash(dataList, branches, true, result);
    return result;
}

std::vector<MerkleDataBlocks> DataMiningManager::splitListIntoPair(std::vector<std::string> &vector,
                                                                         const bool isHahsing) {
    std::vector<MerkleDataBlocks> result;

    if (vector.empty())
        return result;

    if (isHahsing)
        hashingElements(vector);

    int position = 0;
    int step = 2;
    const int sizeVector = vector.size();
    bool isLastPair = sizeVector <= 2;
    const bool isPairVector = (sizeVector % 2 == 0) ? true : false;
    const int next = 1;

    while (position < sizeVector) {
        std::vector<std::string> pair;
        if (isLastPair) {
            pair.push_back(vector[position]);
            if (isLastPair)
                pair.push_back(vector[position + next]);
        } else {
            pair.push_back(vector[position]);
            pair.push_back(vector[position + next]);
        }

        if (!isPairVector) {
            position += ((position + step) > sizeVector) ? 1 : 2;
            isLastPair = ((sizeVector - 1) - position) < 1;
        } else {
            position += step;
            isLastPair = (sizeVector - position) < 2;
        }

        result.push_back(pair);
    }
    return result;
}

void DataMiningManager::hashingElements(std::vector<std::string> &vector) {
    for (int i = 0; i < vector.size(); i++) {
        vector[i] = Utils::calcHash(vector[i]);
    }
}

std::string DataMiningManager::merkleFormula(const std::string &hash1, const std::string &hash2) const {
    return Utils::calcHash(hash1 + hash2);
}
