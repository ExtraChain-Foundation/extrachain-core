#ifndef INDEX_H
#define INDEX_H

#include <QString>
#include <QByteArray>
#include <QDir>
#include <QObject>
#include <QDataStream>
#include "utils/bignumber.h"
#include "utils/utils.h"
#include "datastorage/transaction.h"

/**
 * FileIndex - class for storing different objects as files on disk.
 * Files are stored in folders, called "sections".
 *
 * Example:
 *  BLOCKCHAIN_INDEX/FOLDER_NAME/1/"first_1000_blocks_here" (1 - 999)
 *  BLOCKCHAIN_INDEX/FOLDER_NAME/2/"second_1000_blocks_here" (1000 - 1999)
 */
class FileIndex : public QObject
{
    Q_OBJECT
protected:
    // config //
    QString folderName;          // set in subclasses
    int sectionSize;             // todo: 0 = use only one folder
    BigNumber recordsLimit = -1; // -1 = no limit

    // current state //
    BigNumber records = 0;
    BigNumber firstSavedId = -1;
    BigNumber lastSavedId = -1;

public:
    FileIndex(const QString &folderName, int sectionSize = Config::DataStorage::SECTION_SIZE);

private:
    /**
     * @brief Loads filename from section by two conditions
     * @param getFolder - function that determinate which FOLDER (SECTION) to take from list
     * @param getFile - function that determinate which FILE to take from list
     * @return fileNumber
     */
    BigNumber loadFileFromSection(std::function<QString(const QStringList &folders)> getFolder,
                                  std::function<QString(const QStringList &files)> getFile);

protected:
    BigNumber loadFirstId();
    BigNumber loadLastId();
    BigNumber calcSection(BigNumber id) const;
    BigNumber getFirstSection() const;
    BigNumber getLastSection() const;

    /**
     * Detect section by id and build path.
     * Also creates a directory, if not exists.
     * @param blockId (1023)
     * @return path to block (blockchain/blocks/2/1023)
     */
    QString buildFilePath(const BigNumber &id) const;

    /**
     * @return folder name
     */
    QString getFolderName() const;

    /**
     * @return true, if there is a record limit
     */
    bool hasRecordLimit() const;

    /**
     * @return true, if record limit is reached
     */
    bool recordLimitIsReached() const;

public:
    /**
     * Removes all block files and sections
     */
    void removeAll();

    /**
     * Get relative folder path to index. This is a root index folder path.
     * index/FOLDER_NAME
     * @return folder path
     */
    QString getFolderPath() const;

    /**
     * Get file by id
     * @param id
     * @return record data
     */
    QByteArray getById(const BigNumber &id) const;

    /**
     * Add a record with
     * @param id - record id
     * @param data - serialized record
     * @return resultCode, 0 - adding is successful
     */
    int add(const BigNumber &id, const QByteArray &data);

    /**
     * Removes records with specified id and all next records
     * @param id - record id
     * @return resultCode, 0 - removing is successful
     */
    int removeById(const BigNumber &id);

    /**
     * Removes "count" records from the last record
     * uses removeById.
     * @param count
     * @return resultCode, 0 - removing is successful
     */
    int removeFromEnd(const BigNumber &count);

    BigNumber getFirstSavedId() const;
    BigNumber getLastSavedId() const;
    BigNumber getRecords() const;
};

#endif // INDEX_H
