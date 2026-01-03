#pragma once
#include <string>
#include <map>

namespace LCHBOT {

#define FRAMEWORK_NAME "LCHBOT"
#define FRAMEWORK_VERSION "1.0.0"

enum class Language { ZH, EN };

enum class ErrorCode : int {
    SUCCESS = 0,
    
    CORE_CONFIG_LOAD_FAILED = 1001,
    CORE_INIT_FAILED = 1002,
    
    NETWORK_CONNECTION_FAILED = 2001,
    NETWORK_TIMEOUT = 2002,
    NETWORK_URL_PARSE_FAILED = 2004,
    
    AI_API_ERROR = 3001,
    AI_API_RATE_LIMIT = 3002,
    AI_API_INVALID_KEY = 3003,
    AI_API_EMPTY_RESPONSE = 3004,
    AI_API_UNKNOWN_FORMAT = 3006,
    
    PLUGIN_EXEC_ERROR = 4002,
    
    ASTRBOT_HANDLER_ERROR = 5001,
    ASTRBOT_COMMAND_ERROR = 5002,
    ASTRBOT_API_ERROR = 5003,
    
    DB_CONNECTION_FAILED = 6001,
    DB_QUERY_FAILED = 6002
};

struct BilingualText {
    std::string zh;
    std::string en;
    std::string get(Language lang) const { return lang == Language::ZH ? zh : en; }
};

class ErrorSystem {
public:
    static ErrorSystem& instance() {
        static ErrorSystem inst;
        return inst;
    }
    
    void setLanguage(Language lang) { lang_ = lang; }
    Language getLanguage() const { return lang_; }
    
    std::string formatError(ErrorCode code, const std::string& detail = "") {
        int code_num = static_cast<int>(code);
        std::string module_name = getModuleName(code_num);
        auto desc = getErrorDescription(code);
        
        std::string msg;
        if (lang_ == Language::ZH) {
            msg = "[" FRAMEWORK_NAME "] \xe9\x94\x99\xe8\xaf\xaf #" + std::to_string(code_num) + 
                  " [" + module_name + "] " + desc.zh;
            if (!detail.empty()) msg += " | \xe8\xaf\xa6\xe6\x83\x85: " + detail;
        } else {
            msg = "[" FRAMEWORK_NAME "] Error #" + std::to_string(code_num) + 
                  " [" + module_name + "] " + desc.en;
            if (!detail.empty()) msg += " | Detail: " + detail;
        }
        return msg;
    }
    
    std::string formatUserError(ErrorCode code) {
        int code_num = static_cast<int>(code);
        std::string module_name = getModuleName(code_num);
        auto user_msg = getUserMessage(code);
        if (lang_ == Language::ZH) {
            return "[" FRAMEWORK_NAME "] " + user_msg.zh + " [" + module_name + " #" + std::to_string(code_num) + "]";
        }
        return "[" FRAMEWORK_NAME "] " + user_msg.en + " [" + module_name + " #" + std::to_string(code_num) + "]";
    }
    
private:
    ErrorSystem() : lang_(Language::ZH) {
        initDescriptions();
        initUserMessages();
    }
    
    std::string getModuleName(int code) {
        if (code >= 1000 && code < 2000) return "Core";
        if (code >= 2000 && code < 3000) return "Network";
        if (code >= 3000 && code < 4000) return "AI";
        if (code >= 4000 && code < 5000) return "Plugin";
        if (code >= 5000 && code < 6000) return "AstrBot";
        if (code >= 6000 && code < 7000) return "Database";
        return "Unknown";
    }
    
    BilingualText getErrorDescription(ErrorCode code) {
        auto it = descriptions_.find(code);
        return it != descriptions_.end() ? it->second : BilingualText{"\xe6\x9c\xaa\xe7\x9f\xa5\xe9\x94\x99\xe8\xaf\xaf", "Unknown error"};
    }
    
    BilingualText getUserMessage(ErrorCode code) {
        auto it = user_messages_.find(code);
        return it != user_messages_.end() ? it->second : BilingualText{"\xe6\x9c\x8d\xe5\x8a\xa1\xe6\x9a\x82\xe6\x97\xb6\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8", "Service unavailable"};
    }
    
    void initDescriptions() {
        descriptions_[ErrorCode::CORE_CONFIG_LOAD_FAILED] = {"\xe9\x85\x8d\xe7\xbd\xae\xe6\x96\x87\xe4\xbb\xb6\xe5\x8a\xa0\xe8\xbd\xbd\xe5\xa4\xb1\xe8\xb4\xa5", "Config load failed"};
        descriptions_[ErrorCode::CORE_INIT_FAILED] = {"\xe6\xa0\xb8\xe5\xbf\x83\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe5\xa4\xb1\xe8\xb4\xa5", "Core init failed"};
        descriptions_[ErrorCode::NETWORK_CONNECTION_FAILED] = {"\xe7\xbd\x91\xe7\xbb\x9c\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5", "Network connection failed"};
        descriptions_[ErrorCode::NETWORK_TIMEOUT] = {"\xe7\xbd\x91\xe7\xbb\x9c\xe8\xaf\xb7\xe6\xb1\x82\xe8\xb6\x85\xe6\x97\xb6", "Network timeout"};
        descriptions_[ErrorCode::AI_API_ERROR] = {"AI API\xe6\x9c\x8d\xe5\x8a\xa1\xe9\x94\x99\xe8\xaf\xaf", "AI API error"};
        descriptions_[ErrorCode::AI_API_RATE_LIMIT] = {"AI API\xe8\xaf\xb7\xe6\xb1\x82\xe9\xa2\x91\xe7\x8e\x87\xe8\xb6\x85\xe9\x99\x90", "AI API rate limit"};
        descriptions_[ErrorCode::AI_API_INVALID_KEY] = {"AI API\xe5\xaf\x86\xe9\x92\xa5\xe6\x97\xa0\xe6\x95\x88", "AI API invalid key"};
        descriptions_[ErrorCode::AI_API_EMPTY_RESPONSE] = {"AI API\xe8\xbf\x94\xe5\x9b\x9e\xe7\xa9\xba\xe5\x93\x8d\xe5\xba\x94", "AI API empty response"};
        descriptions_[ErrorCode::AI_API_UNKNOWN_FORMAT] = {"AI API\xe5\x93\x8d\xe5\xba\x94\xe6\xa0\xbc\xe5\xbc\x8f\xe6\x9c\xaa\xe7\x9f\xa5", "AI API unknown format"};
        descriptions_[ErrorCode::PLUGIN_EXEC_ERROR] = {"\xe6\x8f\x92\xe4\xbb\xb6\xe6\x89\xa7\xe8\xa1\x8c\xe9\x94\x99\xe8\xaf\xaf", "Plugin exec error"};
        descriptions_[ErrorCode::ASTRBOT_HANDLER_ERROR] = {"AstrBot\xe5\xa4\x84\xe7\x90\x86\xe5\x99\xa8\xe9\x94\x99\xe8\xaf\xaf", "AstrBot handler error"};
        descriptions_[ErrorCode::ASTRBOT_COMMAND_ERROR] = {"AstrBot\xe5\x91\xbd\xe4\xbb\xa4\xe6\x89\xa7\xe8\xa1\x8c\xe9\x94\x99\xe8\xaf\xaf", "AstrBot command error"};
        descriptions_[ErrorCode::ASTRBOT_API_ERROR] = {"AstrBot API\xe8\xb0\x83\xe7\x94\xa8\xe9\x94\x99\xe8\xaf\xaf", "AstrBot API error"};
        descriptions_[ErrorCode::DB_CONNECTION_FAILED] = {"\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5", "Database connection failed"};
        descriptions_[ErrorCode::DB_QUERY_FAILED] = {"\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe6\x9f\xa5\xe8\xaf\xa2\xe5\xa4\xb1\xe8\xb4\xa5", "Database query failed"};
    }
    
    void initUserMessages() {
        user_messages_[ErrorCode::AI_API_ERROR] = {"AI\xe6\x9c\x8d\xe5\x8a\xa1\xe6\x9a\x82\xe6\x97\xb6\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8", "AI service unavailable"};
        user_messages_[ErrorCode::AI_API_RATE_LIMIT] = {"AI\xe6\x9c\x8d\xe5\x8a\xa1\xe7\xb9\x81\xe5\xbf\x99,\xe8\xaf\xb7\xe7\xa8\x8d\xe5\x90\x8e\xe9\x87\x8d\xe8\xaf\x95", "AI service busy, retry later"};
        user_messages_[ErrorCode::AI_API_INVALID_KEY] = {"AI\xe6\x9c\x8d\xe5\x8a\xa1\xe9\x85\x8d\xe7\xbd\xae\xe9\x94\x99\xe8\xaf\xaf", "AI service config error"};
        user_messages_[ErrorCode::AI_API_EMPTY_RESPONSE] = {"AI\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x93\x8d\xe5\xba\x94\xe5\xbc\x82\xe5\xb8\xb8", "AI service response error"};
        user_messages_[ErrorCode::AI_API_UNKNOWN_FORMAT] = {"AI\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x93\x8d\xe5\xba\x94\xe5\xbc\x82\xe5\xb8\xb8", "AI service response error"};
        user_messages_[ErrorCode::ASTRBOT_HANDLER_ERROR] = {"\xe6\x8f\x92\xe4\xbb\xb6\xe5\xa4\x84\xe7\x90\x86\xe5\x87\xba\xe9\x94\x99", "Plugin handler error"};
        user_messages_[ErrorCode::ASTRBOT_COMMAND_ERROR] = {"\xe5\x91\xbd\xe4\xbb\xa4\xe6\x89\xa7\xe8\xa1\x8c\xe5\x87\xba\xe9\x94\x99", "Command execution error"};
        user_messages_[ErrorCode::ASTRBOT_API_ERROR] = {"\xe6\x8f\x92\xe4\xbb\xb6" "API\xe8\xb0\x83\xe7\x94\xa8\xe5\x87\xba\xe9\x94\x99", "Plugin API error"};
        user_messages_[ErrorCode::DB_CONNECTION_FAILED] = {"\xe6\x95\xb0\xe6\x8d\xae\xe6\x9c\x8d\xe5\x8a\xa1\xe6\x9a\x82\xe6\x97\xb6\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8", "Data service unavailable"};
        user_messages_[ErrorCode::DB_QUERY_FAILED] = {"\xe6\x95\xb0\xe6\x8d\xae\xe6\x9f\xa5\xe8\xaf\xa2\xe5\xa4\xb1\xe8\xb4\xa5", "Data query failed"};
    }
    
    Language lang_;
    std::map<ErrorCode, BilingualText> descriptions_;
    std::map<ErrorCode, BilingualText> user_messages_;
};

}
