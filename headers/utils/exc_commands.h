/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <QObject>
#include <QMutex>
#include <QString>
#include <QCoreApplication>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

#include <cstdio>

class SimpleConsole : public QObject {
    Q_OBJECT

public:
    // Single function to start the console.
    // interactive == false starts a headless mode (no terminal, no input thread) —
    // suitable for running under Docker or as a daemon without a controlling tty.
    static SimpleConsole* start(std::function<void(const std::string&)> commandHandler,
                                bool interactive = true) {
        auto& instance = instance_storage();
        if (!instance) {
            instance                   = new SimpleConsole();
            instance->m_commandHandler = commandHandler;
            instance->init(interactive);
        }
        return instance;
    }

    // Functions for logging
    static void preserveInput() {
        SimpleConsole* instance = getInstance();
        if (instance) {
            QMutexLocker locker(&instance->m_mutex);
            printf("\r\033[K");
            fflush(stdout);
        }
    }

    static void restoreInput() {
        SimpleConsole* instance = getInstance();
        if (instance) {
            QMutexLocker locker(&instance->m_mutex);
            printf("> ");
            if (!instance->m_currentInput.empty()) {
                printf("%s", instance->m_currentInput.c_str());
                int moveBack = static_cast<int>(instance->m_currentInput.length() - instance->m_cursorPos);
                if (moveBack > 0)
                    printf("\033[%dD", moveBack);
            }
            fflush(stdout);
        }
    }

    ~SimpleConsole() {
        m_running = false;
        if (m_interactive) {
            restoreTerminal();
        }
        if (m_inputThread.joinable()) {
            m_inputThread.join();
        }
    }

private:
    SimpleConsole() = default;

    static SimpleConsole* getInstance() {
        return instance_storage();
    }

    static SimpleConsole*& instance_storage() {
        static SimpleConsole* instance = nullptr;
        return instance;
    }

    void init(bool interactive) {
        connect(this, &SimpleConsole::commandEntered, this, &SimpleConsole::handleCommand, Qt::QueuedConnection);

#ifndef _WIN32
        // No controlling terminal (e.g. Docker without -it): never enter interactive
        // mode, otherwise read(STDIN_FILENO) busy-loops on EOF and join() hangs on exit.
        if (interactive && !isatty(STDIN_FILENO)) {
            interactive = false;
        }
#endif

        m_interactive = interactive;
        if (!m_interactive) {
            // Headless / daemon mode: rely solely on the Qt event loop and OS signals.
            return;
        }

        setupTerminal();
        m_running     = true;
        m_inputThread = std::thread(&SimpleConsole::inputLoop, this);
        printf("> ");
        fflush(stdout);
    }

    void setupTerminal() {
#ifdef _WIN32
        m_hStdin = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(m_hStdin, &m_oldConsoleMode);
        SetConsoleMode(m_hStdin, 0); // Disable line input and echo
#else
        tcgetattr(STDIN_FILENO, &m_oldTermios);
        struct termios newTermios = m_oldTermios;
        newTermios.c_lflag &= ~(ICANON | ECHO);
        newTermios.c_cc[VMIN]  = 0;
        newTermios.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
#endif
    }

    void restoreTerminal() {
#ifdef _WIN32
        SetConsoleMode(m_hStdin, m_oldConsoleMode);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &m_oldTermios);
#endif
    }

    void inputLoop() {
        char ch;
        while (m_running) {
#ifdef _WIN32
            if (_kbhit()) {
                ch = _getch();
#else
            if (read(STDIN_FILENO, &ch, 1) > 0) {
#endif
                QMutexLocker locker(&m_mutex);

                if (ch == '\n' || ch == '\r') {
                    if (!m_currentInput.empty()) {
                        std::string command = m_currentInput;
                        m_currentInput.clear();
                        m_cursorPos = 0;
                        printf("\n");
                        fflush(stdout);
                        emit commandEntered(QString::fromStdString(command));
                    } else {
                        printf("\n> ");
                        fflush(stdout);
                    }
#ifdef _WIN32
                } else if (ch == 8) { // Backspace on Windows
#else
                } else if (ch == 127 || ch == 8) { // Backspace on Unix
#endif
                    if (m_cursorPos > 0) {
                        m_currentInput.erase(m_cursorPos - 1, 1);
                        m_cursorPos--;
                        redrawLine();
                    }
                } else if (ch >= 32 && ch <= 126) { // Printable characters
                    m_currentInput.insert(m_cursorPos, 1, ch);
                    m_cursorPos++;
                    redrawLine();
                } else if (ch == 3) { // Ctrl+C
                    printf("\nExiting...\n");
                    m_running = false;
                    QCoreApplication::quit();
                    break;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void redrawLine() {
        size_t savedPos = m_cursorPos;
        printf("\r\033[K> ");
        printf("%s", m_currentInput.c_str());
        int moveBack = static_cast<int>(m_currentInput.length() - savedPos);
        if (moveBack > 0)
            printf("\033[%dD", moveBack);
        fflush(stdout);
    }

signals:
    void commandEntered(const QString& command);

private slots:
    void handleCommand(const QString& command) {
        if (m_commandHandler) {
            m_commandHandler(command.toStdString());
        }
    }

private:
    std::thread                             m_inputThread;
    std::atomic<bool>                       m_running { false };
    bool                                    m_interactive { true };
    QMutex                                  m_mutex;
    std::string                             m_currentInput;
    size_t                                  m_cursorPos = 0;
    std::function<void(const std::string&)> m_commandHandler;

#ifdef _WIN32
    HANDLE m_hStdin;
    DWORD  m_oldConsoleMode;
#else
    struct termios m_oldTermios;
#endif
};
