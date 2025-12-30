#include "bot/Bot.h"
#include "core/Logger.h"
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
    
    std::cout << R"(
  _     _____ _   _ ____   ___ _____ 
 | |   / ____| | | |  _ \ / _ \_   _|
 | |  | |    | |_| | |_) | | | || |  
 | |  | |    |  _  |  _ <| |_| || |  
 | |__| |____| | | | |_) | |_| || |_ 
 |_____\_____|_| |_|____/ \___/_____|
                                     
    QQ Bot Framework v1.0.0
    OneBot 11 Protocol Support
)" << std::endl;
    
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
    
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}
