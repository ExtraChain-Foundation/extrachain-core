#include "utils/wasm.h"

extern "C" {
#include <m3_env.h>
#include <wasm3.h>
}

#define FATAL(func, msg)                                       \
    {                                                          \
        qDebug() << __LINE__ << "Fatal" << func << ":" << msg; \
        return;                                                \
    }
#define WASM_STACK_SLOTS (2 * 1024)
#define NATIVE_STACK_SIZE (32 * 1024)
//#define WASM_MEMORY_LIMIT 6400000

std::string buf_to_str(const uint8_t* buf, uint32_t len) {
    char* test = new char[len];
    for (int i = 0; i != len; i++) {
        test[i] = buf[i];
    }

    std::string s(test, len);
    // delete[] test;
    return s;
}

m3ApiRawFunction(m3_arduino_print) {
    m3ApiGetArgMem(const uint8_t*, buf);
    m3ApiGetArg(uint32_t, len);

    auto str = buf_to_str(buf, len);
    qInfo() << "[WASM]" << str.c_str();

    m3ApiSuccess();
}

// m3ApiRawFunction(m3_some) {
//     m3ApiReturnType(const uint8_t*);

//    auto q = new uint8_t[2];
//    q[0] = 57;
//    q[1] = 58;

//    qDebug() << "[WASM] send some";
//    m3ApiReturn(q);
//}

M3Result linkFunctions(IM3Runtime runtime) {
    IM3Module module = runtime->modules;
    m3_LinkRawFunction(module, "test", "print", "v(*i)", &m3_arduino_print);
    // m3_LinkRawFunction(module, "test", "some", "v(*i)", &m3_some);
    // m3_LinkRawFunction(module, "test", "some", "*i()", &m3_some);
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

    qDebug() << ("Running WebAssembly...");

    result = m3_CallV(f, 4);

    if (result) {
        M3ErrorInfo info;
        m3_GetErrorInfo(runtime, &info);
        qDebug() << "Error: " << result << info.message;
        if (info.file && strlen(info.file) && info.line) {
            qDebug() << "At" << info.file << info.line;
        }
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
}
