#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Types.h"

namespace LCHBOT {

struct WebSocketConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 3001;
    std::string path = "/";
    std::string token;
    uint32_t heartbeat_interval = 60000;
    uint32_t reconnect_interval = 5000;
    uint32_t max_reconnect_attempts = 10;
};

struct PluginConfig {
    std::string plugins_dir = "plugins";
    std::string python_home;
    bool enable_python = true;
    bool enable_native = true;
    std::vector<std::string> disabled_plugins;
};

struct LogConfig {
    std::string log_dir = "logs";
    std::string log_level = "info";
    bool console_output = true;
    bool file_output = true;
    uint32_t max_file_size = 10485760;
    uint32_t max_files = 10;
};

struct AIConfig {
    std::string api_url = "";
    std::string api_key;
};

struct BotConfig {
    WebSocketConfig websocket;
    PluginConfig plugin;
    LogConfig log;
    AIConfig ai;
    std::string data_dir = "data";
    std::string config_file = "config.ini";
    int admin_port = 8080;
    std::vector<int64_t> master_qq;
};

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager inst;
        return inst;
    }
    
    bool load(const std::string& path) {
        config_path_ = path;
        std::ifstream file(path);
        if (!file.is_open()) {
            return createDefault(path);
        }
        
        std::string line;
        std::string section;
        
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            parseValue(section, key, value);
        }
        
        return true;
    }
    
    bool save() const {
        return save(config_path_);
    }
    
    bool save(const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        
        file << "[websocket]\n";
        file << "host=" << config_.websocket.host << "\n";
        file << "port=" << config_.websocket.port << "\n";
        file << "path=" << config_.websocket.path << "\n";
        file << "token=" << config_.websocket.token << "\n";
        file << "heartbeat_interval=" << config_.websocket.heartbeat_interval << "\n";
        file << "reconnect_interval=" << config_.websocket.reconnect_interval << "\n";
        file << "max_reconnect_attempts=" << config_.websocket.max_reconnect_attempts << "\n";
        file << "\n";
        
        file << "[plugin]\n";
        file << "plugins_dir=" << config_.plugin.plugins_dir << "\n";
        file << "python_home=" << config_.plugin.python_home << "\n";
        file << "enable_python=" << (config_.plugin.enable_python ? "true" : "false") << "\n";
        file << "enable_native=" << (config_.plugin.enable_native ? "true" : "false") << "\n";
        file << "\n";
        
        file << "[log]\n";
        file << "log_dir=" << config_.log.log_dir << "\n";
        file << "log_level=" << config_.log.log_level << "\n";
        file << "console_output=" << (config_.log.console_output ? "true" : "false") << "\n";
        file << "file_output=" << (config_.log.file_output ? "true" : "false") << "\n";
        file << "max_file_size=" << config_.log.max_file_size << "\n";
        file << "max_files=" << config_.log.max_files << "\n";
        file << "\n";
        
        file << "[general]\n";
        file << "data_dir=" << config_.data_dir << "\n";
        file << "admin_port=" << config_.admin_port << "\n";
        if (!config_.master_qq.empty()) {
            file << "master_qq=";
            for (size_t i = 0; i < config_.master_qq.size(); ++i) {
                if (i > 0) file << ",";
                file << config_.master_qq[i];
            }
            file << "\n";
        }
        
        return true;
    }
    
    BotConfig& config() { return config_; }
    const BotConfig& config() const { return config_; }
    
private:
    ConfigManager() = default;
    
    bool createDefault(const std::string& path) {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        return save(path);
    }
    
    void parseValue(const std::string& section, const std::string& key, const std::string& value) {
        if (section == "websocket") {
            if (key == "host") config_.websocket.host = value;
            else if (key == "port") config_.websocket.port = static_cast<uint16_t>(std::stoi(value));
            else if (key == "path") config_.websocket.path = value;
            else if (key == "token") config_.websocket.token = value;
            else if (key == "heartbeat_interval") config_.websocket.heartbeat_interval = std::stoul(value);
            else if (key == "reconnect_interval") config_.websocket.reconnect_interval = std::stoul(value);
            else if (key == "max_reconnect_attempts") config_.websocket.max_reconnect_attempts = std::stoul(value);
        }
        else if (section == "plugin") {
            if (key == "plugins_dir") config_.plugin.plugins_dir = value;
            else if (key == "python_home") config_.plugin.python_home = value;
            else if (key == "enable_python") config_.plugin.enable_python = (value == "true" || value == "1");
            else if (key == "enable_native") config_.plugin.enable_native = (value == "true" || value == "1");
        }
        else if (section == "log") {
            if (key == "log_dir") config_.log.log_dir = value;
            else if (key == "log_level") config_.log.log_level = value;
            else if (key == "console_output") config_.log.console_output = (value == "true" || value == "1");
            else if (key == "file_output") config_.log.file_output = (value == "true" || value == "1");
            else if (key == "max_file_size") config_.log.max_file_size = std::stoul(value);
            else if (key == "max_files") config_.log.max_files = std::stoul(value);
        }
        else if (section == "general") {
            if (key == "data_dir") config_.data_dir = value;
            else if (key == "admin_port") config_.admin_port = std::stoi(value);
            else if (key == "master_qq") {
                config_.master_qq.clear();
                std::stringstream ss(value);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    token = trim(token);
                    if (!token.empty()) {
                        config_.master_qq.push_back(std::stoll(token));
                    }
                }
            }
        }
        else if (section == "ai") {
            if (key == "api_url") config_.ai.api_url = value;
            else if (key == "api_key") config_.ai.api_key = value;
        }
    }
    
    std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
    
    BotConfig config_;
    std::string config_path_;
};

}
