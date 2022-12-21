#pragma once

#include "wasm3/wasm3_cpp.h"
#include <QObject>
#include <fstream>
#include <iostream>
#include <string>

static const int NO_RESULT = -1;

class RustFuncTest : public QObject {
    Q_OBJECT
    wasm3::environment env;
    std::string filetest;

public:
    enum class MakeProcess {
        Nothing,
        CreateTransaction
    };

    enum class WASM_ERROR_TYPE {
        NoFunction
    };

    RustFuncTest(const std::string &namefile, QObject *object = nullptr)
        : QObject(object)
        , filetest(namefile) {
        env = wasm3::environment();

        connect(this, &RustFuncTest::runned, this,
                [=](std::string actor, std::string nameFunction, MakeProcess process) {
                    std::cout << "function '" << nameFunction << "' runned slot by '" << actor << "'."
                              << std::endl;

                    switch (process) {
                    case MakeProcess::Nothing: {
                        std::cout << "No process." << std::endl;
                        break;
                    }
                    case MakeProcess::CreateTransaction: {
                        std::cout << "Create transaction." << std::endl;
                        break;
                    }
                    }
                });

        connect(this, &RustFuncTest::error, this,
                [=](std::string actor, std::string nameFunction, WASM_ERROR_TYPE error) {
                    switch (error) {
                    case WASM_ERROR_TYPE::NoFunction: {
                        std::cout << "Function " << nameFunction << " doesn't exist." << std::endl;
                        break;
                    }
                    }
                });
    }

    void runTestFunctions() {
        std::cout << "start wasm rust functions tests..." << std::endl;
        wasm3::runtime runtime = env.new_runtime(1024);
        std::ifstream wasm_file(filetest, std::ios::binary | std::ios::in);
        if (!wasm_file.is_open()) {
            throw std::runtime_error("Failed to open wasm file");
        }

        wasm3::module mod = env.parse_module(wasm_file);
        runtime.load(mod);

        const char *name_function = "start";            // exist
        const char *name_function_not_exist = "start1"; // not exist
        bool funcExist = existFunction(runtime, name_function);
        std::cout << "Function " << name_function << (funcExist ? " exist" : " not exist") << std::endl;

        funcExist = existFunction(runtime, name_function_not_exist);
        std::cout << "Function " << name_function_not_exist << (funcExist ? " exist" : " not exist")
                  << std::endl;

        std::cout << "finish wasm rust functions tests." << std::endl;

        std::cout << "test add func with parameters: 3,4" << std::endl;
        funcExist = existFunction(runtime, "add");
        std::cout << "function add is " << (funcExist ? "exist" : "not exist") << std::endl;
        if (funcExist) {
            int res = callFunc<int>("ACTOR", runtime, "add", MakeProcess::Nothing, 3, 4);
            std::cout << "result function add is " << res << std::endl;
        }
        std::cout << "finish test add func." << std::endl;
    }

    bool existFunction(wasm3::runtime runtime, const char *name_func) {
        try {
            wasm3::function memcpy_test_fn = runtime.find_function(name_func);
        } catch (std::runtime_error &e) {
            return false;
        }
        return true;
    }

    template <typename Ret = void, typename... Args>
    Ret callFunc(std::string actor, wasm3::runtime runtime, const char *name_func, MakeProcess runProcess,
                 Args... args) {
        if (existFunction(runtime, name_func)) {
            wasm3::function memcpy_test_fn = runtime.find_function(name_func);
            const auto result = memcpy_test_fn.call<Ret>(args...);
            emit runned(actor, name_func, runProcess);
            return result;
        } else {
            emit error(actor, name_func, RustFuncTest::WASM_ERROR_TYPE::NoFunction);
        }
        return NO_RESULT;
    }

signals:
    void runned(std::string actor, std::string func, MakeProcess process);
    void error(std::string actor, std::string func, RustFuncTest::WASM_ERROR_TYPE type);
};
