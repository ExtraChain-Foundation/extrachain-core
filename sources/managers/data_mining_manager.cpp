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
#include <QDebug>

DataMiningManager::DataMiningManager(QObject *parent)
    : QObject(parent) {
}

std::string DataMiningManager::merkleHash(std::vector<std::string> &listHashes,
                                          std::vector<std::vector<std::string>> &branchesTree,
                                          const bool isHahsing) {
    std::string rootHash = "";
    const auto splittedList = splitListOnPair(listHashes, isHahsing);

    std::vector<std::string> lastResult;
    qDebug() << "print splitted list";
    for(int i = 0; i < splittedList.size(); i++) {
        const auto pair = splittedList[i];
        for(int j = 0; j < pair.size(); j++) {
            qDebug() << pair[j].c_str();
        }
        qDebug() << "========" << pair.size();
    }    qDebug() << "finish print splitted list";

    for (int i = 0; i < splittedList.size(); i++) {
        const auto pair = splittedList[i];

        if (pair.size() == 1) {
            lastResult.push_back(pair[0]);
        } else {
            lastResult.push_back(merkleFormula(pair[0], pair[1]));
        }
    }
    branchesTree.push_back(lastResult);

    for (int k = 0; k < lastResult.size(); k++) {
        qDebug() << lastResult[k].c_str();
    }

    if (lastResult.size() != 1) {
        merkleHash(lastResult, branchesTree, false);
    } else {
        qDebug() << "Root hash: " << branchesTree[branchesTree.size() -1][0].c_str();
    }

    return rootHash;
}

std::vector<std::vector<std::string>> DataMiningManager::splitListOnPair(std::vector<std::string> &vector,
                                                                         const bool isHahsing) {
    std::vector<std::vector<std::string>> result;

    if (vector.empty())
        return result;

    if(isHahsing)
        hashingElements(vector);

    int position = 0;
    int step = 2;
    const int sizeVector = vector.size();
    bool isLastPair = sizeVector <= 2;
    const bool isPairVector = sizeVector % 2 == 0 ? true : false;
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
        std::string res = "[" + vector[i] + "|";
        vector[i] = Utils::calcHash(vector[i]);
        res += vector[i] + "]";
        qDebug() << res.c_str();
    }
}

std::string DataMiningManager::merkleFormula(const std::string &hash1, const std::string &hash2) const {
    return Utils::calcHash(hash1 + hash2);
}
