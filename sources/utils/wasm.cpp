#include "utils/wasm.h"

#include <QFile>
#include <QString>

#include "utils/exc_utils.h"

extern "C" {
#include <m3_env.h>
#include <wasm3.h>
}

#define FATAL(func, msg)                                                 \
    {                                                                    \
        qDebug() << "[WASM] Fatal on" << __LINE__ << func << ":" << msg; \
        return;                                                          \
    }

#define WASM_STACK_SLOTS (2 * 1024 * 2)
#define NATIVE_STACK_SIZE (32 * 1024 * 2)
//#define WASM_MEMORY_LIMIT 6400000

std::string u8bufToStdString(const uint8_t* buf, uint32_t len) {
    char* test = new char[len];
    for (int i = 0; i != len; i++) {
        test[i] = buf[i];
    }

    std::string s(test, len);
    // delete[] test;
    return s;
}

m3ApiRawFunction(console_print) {
    m3ApiGetArgMem(const uint8_t*, buf);
    m3ApiGetArg(uint32_t, len);

    auto str = u8bufToStdString(buf, len);
    qInfo() << "[WASM]" << str.c_str();

    m3ApiSuccess();
}

m3ApiRawFunction(dfs_file_size) {
    m3ApiReturnType(int64_t);
    m3ApiGetArgMem(const uint8_t*, file_name);
    m3ApiGetArg(uint64_t, file_length);

    auto str = u8bufToStdString(file_name, file_length);
    QFileInfo fileInfo(QString::fromStdString(str));

    m3ApiReturn(fileInfo.size())
}

m3ApiRawFunction(dfs_file_read) {
    m3ApiReturnType(int32_t);

    m3ApiGetArg(uint64_t, offset);
    m3ApiGetArgMem(uint8_t*, buf);
    m3ApiGetArgMem(const uint8_t*, file_name);
    m3ApiGetArg(uint64_t, file_length);

    auto fileName = u8bufToStdString(file_name, file_length);
    QFile file(QString::fromStdString(fileName));
    if (!file.open(QFile::ReadOnly)) {
        qFatal("wasm file error");
    }
    file.seek(offset);
    auto bytes = file.read(1024);
    for (int i = 0; i != bytes.size(); i++) {
        buf[i] = bytes[i];
    }

    m3ApiReturn(bytes.size());
}

M3Result linkFunctions(IM3Runtime runtime) {
    IM3Module module = runtime->modules;
    m3_LinkRawFunction(module, "test", "print", "v(*i)", &console_print);
    m3_LinkRawFunction(module, "test", "dfs_file_size", "I(*i)", &dfs_file_size);
    m3_LinkRawFunction(module, "test", "dfs_file_read", "i(I**i)", &dfs_file_read);
    return m3Err_none;
}

void wasm::wasm_task(const std::string& filePath) {
    QFile file(QString::fromStdString(filePath));
    file.open(QFile::ReadOnly);
    QByteArray read = file.readAll();
    file.close();

    const unsigned char* const app_wasm = reinterpret_cast<unsigned char*>(read.data());
    int app_wasm_len = read.size();

    M3Result result = m3Err_none;

    IM3Environment env = m3_NewEnvironment();
    if (!env)
        FATAL("NewEnvironment", "failed");

    IM3Runtime runtime = m3_NewRuntime(env, WASM_STACK_SLOTS, NULL);
    if (!runtime)
        FATAL("NewRuntime", "failed");

#ifdef WASM_MEMORY_LIMIT
    runtime->memoryLimit = WASM_MEMORY_LIMIT;
#endif

    IM3Module module;
    result = m3_ParseModule(env, &module, app_wasm, app_wasm_len);
    if (result)
        FATAL("ParseModule", result);

    result = m3_LoadModule(runtime, module);
    if (result)
        FATAL("LoadModule", result);

    result = linkFunctions(runtime);
    if (result)
        FATAL("LinkTest", result);

    IM3Function f;
    result = m3_FindFunction(&f, runtime, "_start");
    if (result)
        FATAL("FindFunction", result);

    result = m3_CallV(f, 4);

    if (result) {
        M3ErrorInfo info;
        m3_GetErrorInfo(runtime, &info);
        qDebug() << "[WASM] Error:" << result << info.message;
        if (info.file && strlen(info.file) && info.line) {
            qDebug() << "At" << info.file << info.line;
        }
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
}
