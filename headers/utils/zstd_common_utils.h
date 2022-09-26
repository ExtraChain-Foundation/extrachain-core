#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <zstd.h>


#if defined(__GNUC__)
#  define UNUSED_ATTR __attribute__((unused))
#else
#  define UNUSED_ATTR
#endif

#define HEADER_FUNCTION static UNUSED_ATTR

typedef enum {
    ERROR_fsize = 1,
    ERROR_fopen = 2,
    ERROR_fclose = 3,
    ERROR_fread = 4,
    ERROR_fwrite = 5,
    ERROR_loadFile = 6,
    ERROR_saveFile = 7,
    ERROR_malloc = 8,
    ERROR_largeFile = 9,
} COMMON_ErrorCode;

#define CHECK(cond, ...)                        \
    do {                                        \
        if (!(cond)) {                          \
            fprintf(stderr,                     \
                    "%s:%d CHECK(%s) failed: ", \
                    __FILE__,                   \
                    __LINE__,                   \
                    #cond);                     \
            fprintf(stderr, "" __VA_ARGS__);    \
            fprintf(stderr, "\n");              \
            exit(1);                            \
        }                                       \
    } while (0)


#define CHECK_ZSTD(fn)                                           \
    do {                                                         \
        size_t const err = (fn);                                 \
        CHECK(!ZSTD_isError(err), "%s", ZSTD_getErrorName(err)); \
    } while (0)


HEADER_FUNCTION size_t fsize_orDie(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) != 0) {
        /* error */
        perror(filename);
        exit(ERROR_fsize);
    }

    off_t const fileSize = st.st_size;
    size_t const size = (size_t)fileSize;

    if ((fileSize < 0) || (fileSize != (off_t)size)) {
        fprintf(stderr, "%s : filesize too large \n", filename);
        exit(ERROR_largeFile);
    }
    return size;
}

HEADER_FUNCTION FILE* fopen_orDie(const char *filename, const char *instruction)
{
    FILE* const inFile = fopen(filename, instruction);
    if (inFile) return inFile;
    perror(filename);
    exit(ERROR_fopen);
}

HEADER_FUNCTION void fclose_orDie(FILE* file)
{
    if (!fclose(file)) { return; };
    perror("fclose");
    exit(ERROR_fclose);
}

HEADER_FUNCTION size_t fread_orDie(void* buffer, size_t sizeToRead, FILE* file)
{
    size_t const readSize = fread(buffer, 1, sizeToRead, file);
    if (readSize == sizeToRead) return readSize;
    if (feof(file)) return readSize;

    perror("fread");
    exit(ERROR_fread);
}


HEADER_FUNCTION size_t fwrite_orDie(const void* buffer, size_t sizeToWrite, FILE* file)
{
    size_t const writtenSize = fwrite(buffer, 1, sizeToWrite, file);
    if (writtenSize == sizeToWrite) return sizeToWrite;   /* good */
    /* error */
    perror("fwrite");
    exit(ERROR_fwrite);
}


HEADER_FUNCTION void* malloc_orDie(size_t size)
{
    void* const buff = malloc(size);
    if (buff) return buff;
    /* error */
    perror("malloc");
    exit(ERROR_malloc);
}

HEADER_FUNCTION size_t loadFile_orDie(const char* fileName, void* buffer, size_t bufferSize)
{
    size_t const fileSize = fsize_orDie(fileName);
    CHECK(fileSize <= bufferSize, "File too large!");

    FILE* const inFile = fopen_orDie(fileName, "rb");
    size_t const readSize = fread(buffer, 1, fileSize, inFile);
    if (readSize != (size_t)fileSize) {
        fprintf(stderr, "fread: %s : %s \n", fileName, strerror(errno));
        exit(ERROR_fread);
    }
    fclose(inFile);  /* can't fail, read only */
    return fileSize;
}

HEADER_FUNCTION void* mallocAndLoadFile_orDie(const char* fileName, size_t* bufferSize)
{
    size_t const fileSize = fsize_orDie(fileName);
    *bufferSize = fileSize;
    void* const buffer = malloc_orDie(*bufferSize);
    loadFile_orDie(fileName, buffer, *bufferSize);
    return buffer;
}

HEADER_FUNCTION void saveFile_orDie(const char* fileName, const void* buff, size_t buffSize)
{
    FILE* const oFile = fopen_orDie(fileName, "wb");
    size_t const wSize = fwrite(buff, 1, buffSize, oFile);
    if (wSize != (size_t)buffSize) {
        fprintf(stderr, "fwrite: %s : %s \n", fileName, strerror(errno));
        exit(ERROR_fwrite);
    }
    if (fclose(oFile)) {
        perror(fileName);
        exit(ERROR_fclose);
    }
}

#endif
