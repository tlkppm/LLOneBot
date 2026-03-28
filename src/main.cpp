#include "bot/Bot.h"
#include "core/Logger.h"
#include "core/ErrorCodes.h"
#include <iostream>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
    std::atomic<bool> g_running{true};
}

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
        LCHBOT::Bot::instance().stop();
    }
}

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        g_running = false;
        LCHBOT::Bot::instance().stop();
        return TRUE;
    }
    return FALSE;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout
        << "\n"
        << "  _     _____ _   _ ____   ___ _____ \n"
        << " | |   / ____| | | |  _ \\/ _ \\_   _|\n"
        << " | |  | |    | |_| | |_) | | | || |  \n"
        << " | |  | |    |  _  |  _ <| |_| || |  \n"
        << " | |__| |____| | | | |_) | |_| || |_ \n"
        << " |_____\\_____|_| |_|____/ \\___/_____|\n"
        << "                                     \n"
        << "    QQ Bot Framework v" << FRAMEWORK_VERSION << "\n"
        << "    OneBot 11 Protocol Support\n"
        << std::endl;
    
    std::string config_path = "config.ini";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    auto& bot = LCHBOT::Bot::instance();
    
    if (!bot.initialize(config_path)) {
        std::cerr << "Failed to initialize bot" << std::endl;
        return 1;
    }
    
    if (!bot.start()) {
        std::cerr << "Failed to start bot" << std::endl;
        return 1;
    }
    
    try {
        while (g_running && bot.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in main loop: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in main loop" << std::endl;
    }
    
    bot.stop();

    return 0;
}
