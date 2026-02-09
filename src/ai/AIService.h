#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include "../core/Logger.h"
#include "../core/ErrorCodes.h"
#include "../core/Calendar.h"
#include "../admin/Statistics.h"
#include "ContextDatabase.h"
#include "PersonalitySystem.h"
#include "../core/GroupMemberCache.h"

namespace LCHBOT {

struct ImageData {
    std::string url;
    std::string base64;
    std::string media_type;
};

struct GeneratedImage {
    std::string base64;
    std::string media_type;
    std::string text;
};

struct ChatMessage {
    std::string role;
    std::string content;
    int64_t timestamp;
};

struct ConversationContext {
    std::deque<ChatMessage> messages;
    int64_t last_active;
    size_t max_messages = 20;
    
    void addMessage(const std::string& role, const std::string& content) {
        ChatMessage msg;
        msg.role = role;
        msg.content = content;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        messages.push_back(msg);
        last_active = msg.timestamp;
        
        while (messages.size() > max_messages) {
            messages.pop_front();
        }
    }
    
    std::string buildContextPrompt() const {
        std::string prompt;
        for (const auto& msg : messages) {
            if (msg.role == "user") {
                prompt += "User: " + msg.content + "\n";
            } else {
                prompt += "Assistant: " + msg.content + "\n";
            }
        }
        return prompt;
    }
    
    void clear() {
        messages.clear();
    }
};

class ContextManager {
public:
    static ContextManager& instance() {
        static ContextManager inst;
        return inst;
    }
    
    ConversationContext& getContext(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::to_string(group_id) + "_" + std::to_string(user_id);
        return contexts_[key];
    }
    
    ConversationContext& getGroupContext(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "g_" + std::to_string(group_id);
        return contexts_[key];
    }
    
    ConversationContext& getPrivateContext(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "p_" + std::to_string(user_id);
        return contexts_[key];
    }
    
    void clearContext(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::to_string(group_id) + "_" + std::to_string(user_id);
        contexts_.erase(key);
    }
    
    void clearGroupContext(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "g_" + std::to_string(group_id);
        contexts_.erase(key);
    }
    
    void clearAllContexts() {
        std::lock_guard<std::mutex> lock(mutex_);
        contexts_.clear();
    }
    
    void cleanupOldContexts(int64_t max_age_seconds = 3600) {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        for (auto it = contexts_.begin(); it != contexts_.end();) {
            if (now - it->second.last_active > max_age_seconds) {
                it = contexts_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
private:
    ContextManager() = default;
    std::map<std::string, ConversationContext> contexts_;
    std::mutex mutex_;
};

struct ModelConfig {
    std::string id;
    std::string name;
    std::string url;
    std::string description;
    std::string format = "json";
    std::string model_name;  
    std::string api_key;     // 模型独立API key, 为空则用全局key
};

class AIService {
public:
    static AIService& instance() {
        static AIService inst;
        return inst;
    }
    
    void loadModels(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_WARN("[AI] Cannot open models config: " + path);
            return;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        file.close();
        
        models_.clear();
        
        size_t current_pos = json.find("\"current\"");
        if (current_pos != std::string::npos) {
            size_t val_start = json.find("\"", current_pos + 10);
            if (val_start != std::string::npos) {
                val_start++;
                size_t val_end = json.find("\"", val_start);
                if (val_end != std::string::npos) {
                    current_model_ = json.substr(val_start, val_end - val_start);
                }
            }
        }
        
        size_t apikey_pos = json.find("\"api_key\"");
        if (apikey_pos != std::string::npos) {
            size_t colon_pos = json.find(":", apikey_pos);
            if (colon_pos != std::string::npos) {
                size_t val_start = json.find("\"", colon_pos);
                if (val_start != std::string::npos) {
                    val_start++;
                    size_t val_end = json.find("\"", val_start);
                    if (val_end != std::string::npos) {
                        api_key_ = json.substr(val_start, val_end - val_start);
                        LOG_INFO("[AI] API key loaded: " + api_key_.substr(0, 8) + "...");
                    }
                }
            }
        }
        
        size_t models_pos = json.find("\"models\"");
        if (models_pos == std::string::npos) return;
        
        size_t block_start = json.find("{", models_pos);
        if (block_start == std::string::npos) return;
        
        int depth = 1;
        size_t block_end = block_start + 1;
        while (block_end < json.size() && depth > 0) {
            if (json[block_end] == '{') depth++;
            else if (json[block_end] == '}') depth--;
            block_end++;
        }
        
        std::string models_block = json.substr(block_start, block_end - block_start);
        
        size_t pos = 0;
        while ((pos = models_block.find("\"", pos)) != std::string::npos) {
            size_t id_start = pos + 1;
            size_t id_end = models_block.find("\"", id_start);
            if (id_end == std::string::npos) break;
            
            std::string model_id = models_block.substr(id_start, id_end - id_start);
            
            size_t obj_start = models_block.find("{", id_end);
            if (obj_start == std::string::npos) break;
            
            size_t obj_end = models_block.find("}", obj_start);
            if (obj_end == std::string::npos) break;
            
            std::string obj = models_block.substr(obj_start, obj_end - obj_start + 1);
            
            ModelConfig cfg;
            cfg.id = model_id;
            
            auto extract = [&obj](const std::string& key) -> std::string {
                size_t kpos = obj.find("\"" + key + "\"");
                if (kpos == std::string::npos) return "";
                size_t vstart = obj.find("\"", kpos + key.length() + 2);
                if (vstart == std::string::npos) return "";
                vstart++;
                size_t vend = obj.find("\"", vstart);
                if (vend == std::string::npos) return "";
                return obj.substr(vstart, vend - vstart);
            };
            
            cfg.name = extract("name");
            cfg.url = extract("url");
            cfg.description = extract("description");
            cfg.format = extract("format");
            cfg.model_name = extract("model_name");
            cfg.api_key = extract("api_key");
            if (cfg.format.empty()) cfg.format = "json";
            
            if (!cfg.url.empty()) {
                models_[model_id] = cfg;
                LOG_INFO("[AI] Loaded model: " + model_id + " (" + cfg.name + ") format=" + cfg.format);
            }
            
            pos = obj_end + 1;
        }
        
        if (!current_model_.empty() && models_.count(current_model_)) {
            api_url_ = models_[current_model_].url;
            LOG_INFO("[AI] Current model: " + current_model_);
        }
        
        loadGroupConversations();
    }
    
    bool switchModel(const std::string& model_id) {
        if (models_.count(model_id) == 0) {
            return false;
        }
        current_model_ = model_id;
        api_url_ = models_[model_id].url;
        LOG_INFO("[AI] Switched to model: " + model_id);
        return true;
    }
    
    std::string getCurrentModel() const {
        return current_model_;
    }
    
    std::string getCurrentModelName() const {
        if (models_.count(current_model_)) {
            return models_.at(current_model_).name;
        }
        return current_model_;
    }
    
    std::vector<std::string> getAvailableModels() const {
        std::vector<std::string> result;
        for (const auto& [id, cfg] : models_) {
            result.push_back(id);
        }
        return result;
    }
    
    std::string getModelInfo(const std::string& model_id) const {
        if (models_.count(model_id) == 0) return "";
        const auto& cfg = models_.at(model_id);
        return cfg.name + " - " + cfg.description;
    }
    
    void setApiUrl(const std::string& url) {
        api_url_ = url;
    }
    
    void setApiKey(const std::string& key) {
        api_key_ = key;
    }
    
    void setSystemPrompt(const std::string& prompt) {
        system_prompt_ = prompt;
    }
    
    void setSetCardFunc(std::function<void(int64_t, int64_t, const std::string&)> func) {
        set_card_func_ = std::move(func);
    }
    
    void setSetTitleFunc(std::function<void(int64_t, int64_t, const std::string&)> func) {
        set_title_func_ = std::move(func);
    }
    
    ErrorCode getLastError() const { return last_error_; }
    std::string getLastErrorDetail() const { return last_error_detail_; }
    void clearLastError() { last_error_ = ErrorCode::SUCCESS; last_error_detail_.clear(); }
    
    std::string chat(const std::string& message, int64_t group_id = 0, int64_t user_id = 0,
                       const std::string& sender_name = "") {
        auto& personality = PersonalitySystem::instance();
        std::string sanitized_message = personality.sanitizeInput(message);
        
        if (getRequestFormat() == "grok" && group_id > 0) {
            if (grok_group_conversations_.count(group_id) == 0) {
                LOG_INFO("[AI] Creating new Grok conversation for group " + std::to_string(group_id));
                std::string response = callGrokNewConversation(sanitized_message);
                if (!response.empty()) {
                    updateGroupConversation(group_id);
                    return response;
                }
            }
        }
        
        std::string context_key;
        if (group_id > 0) {
            context_key = "g_" + std::to_string(group_id);
        } else if (user_id > 0) {
            context_key = "p_" + std::to_string(user_id);
        }
        
        auto& db = ContextDatabase::instance();
        
        std::string personality_prompt;
        if (group_id > 0) {
            personality_prompt = personality.getPromptForGroup(group_id);
        } else {
            personality_prompt = personality.getCurrentPrompt();
        }
        
        std::string system_content;
        if (!personality_prompt.empty()) {
            system_content = personality_prompt;
        }
        
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm_buf;
        localtime_s(&local_tm_buf, &now_time);
        std::tm* local_tm = &local_tm_buf;
        
        std::string time_str = (local_tm->tm_hour < 10 ? "0" : "") + std::to_string(local_tm->tm_hour) + ":" +
            (local_tm->tm_min < 10 ? "0" : "") + std::to_string(local_tm->tm_min) + ":" +
            (local_tm->tm_sec < 10 ? "0" : "") + std::to_string(local_tm->tm_sec);
        std::string cur_time = (local_tm->tm_hour < 10 ? "0" : "") + std::to_string(local_tm->tm_hour) + ":" +
            (local_tm->tm_min < 10 ? "0" : "") + std::to_string(local_tm->tm_min);
        
        std::string db_stats = context_key.empty() ? "" : db.getContextStats(context_key);
        std::string recent_context = context_key.empty() ? "" : db.buildSmartContextPrompt(context_key, sanitized_message);
        
        std::string calendar_info = Calendar::instance().buildCalendarPrompt();
        std::string date_info = "[系统时间] " + time_str + "\n" + calendar_info;
        
        std::string query_ability = "\n[可用工具]\n";
        if (!db_stats.empty()) {
            query_ability += db_stats + "\n";
        }
        query_ability += 
            "[QUERY:holiday=节日名] - 查询节日日期(如春节/中秋/端午等)\n"
            "[QUERY:keyword=关键词] - 搜索聊天记录中包含关键词的消息\n"
            "[QUERY:sender=用户名] - 搜索特定用户的发言记录\n"
            "[QUERY:recent=数量] - 获取最近N条聊天记录(最多50条)\n"
            "[QUERY:users] - 列出活跃用户排行(含QQ号和消息数)\n"
            "[QUERY:summary] - 获取本年度总结数据(发言排行/月度分布/活跃时段)\n"
            "[QUERY:summary=年份] - 获取指定年份的总结\n"
            "[QUERY:date=YYYY-MM-DD] - 查询指定日期的聊天记录(也支持M月D日格式)\n"
            "[QUERY:setcard=QQ号,新名片] - 修改群成员的群名片(仅群聊可用)\n"
            "[QUERY:settitle=QQ号,专属头衔] - 设置群成员的专属头衔(仅群主可用,空字符串=删除头衔)\n\n"
            "[CQ码说明]\n"
            "消息中的[CQ:at,qq=数字,name=昵称]是QQ的@提及,qq=后面的数字就是QQ号,可直接用于setcard等工具。\n"
            "例:用户消息含[CQ:at,qq=123456,name=张三],则该用户QQ号为123456。\n\n"
            "[回复格式]\n"
            "[THINK]\n"
            "(分析→工具调用→推理,可包含多个[QUERY:...])\n"
            "[/THINK]\n"
            "[ANSWER]\n"
            "(最终回复,用户只看到此部分)\n"
            "[/ANSWER]\n\n";
        
        std::string member_list_prompt;
        if (group_id > 0) {
            auto members = GroupMemberCache::instance().getMembers(group_id);
            if (!members.empty()) {
                if (members.size() <= 120) {
                    member_list_prompt = "\n[本群成员列表] (共" + std::to_string(members.size()) + "人,setcard/settitle必须使用此列表中的QQ号)\n";
                    for (size_t i = 0; i < members.size(); i++) {
                        if (i > 0) member_list_prompt += ", ";
                        member_list_prompt += members[i].second + "(" + std::to_string(members[i].first) + ")";
                    }
                    member_list_prompt += "\n\n";
                } else {
                    member_list_prompt = "\n[本群成员] 共" + std::to_string(members.size()) + "人(列表过长已省略,请用[QUERY:users]或[QUERY:sender=昵称]查找QQ号,禁止猜测编造)\n\n";
                }
            }
        }

        std::string context_ability = "[系统指令]\n" + date_info + query_ability + member_list_prompt +
            "[工具使用规则]\n"
            "1.始终使用[THINK]→[ANSWER]格式,即使不需要工具\n"
            "2.[THINK]对用户不可见,[ANSWER]直接发送给用户,禁止在[ANSWER]中包含[QUERY]或[THINK]标签\n"
            "3.工具必须在[THINK]中以[QUERY:类型=参数]调用,仅文字描述不会执行任何操作\n"
            "4.支持一个[THINK]中写多个[QUERY:...],系统会全部执行\n"
            "5.修改群名片:从[CQ:at,qq=XXX]或[本群成员列表]提取QQ号→[QUERY:setcard=QQ号,新名片]\n"
            "6.名片风格:根据用户要求和语境创造性设计,风格要多样化,不要每次都用同一种符号或格式(如总是XX❤YY)\n"
            "7.批量操作:用户说\"全部/所有\"时,先用[QUERY:users]或[QUERY:recent=50]获取信息,再逐一操作\n"
            "8.回复中提及用户时必须使用其原始昵称/群名片,禁止用****或星号替代\n"
            "9.[群聊历史记录]包含本群过往聊天,你可以从中了解用户关系、话题和群内氛围,并据此回答\n"
            "10.⚠️QQ号必须来自[本群成员列表]或消息中的[CQ:at,qq=XXX],严禁凭记忆编造或猜测QQ号,不确定时用[QUERY:sender=昵称]查找\n\n";
        
        std::string user_content;
        if (!recent_context.empty()) {
            user_content += recent_context + "\n";
        }
        user_content += "[当前消息]\n";
        if (!sender_name.empty()) {
            std::string sender_info = sender_name;
            if (user_id > 0) sender_info += "(QQ:" + std::to_string(user_id) + ")";
            user_content += "[" + cur_time + "] " + sender_info + ": " + sanitized_message;
        } else {
            user_content += "[" + cur_time + "] " + sanitized_message;
        }
        
        std::string full_prompt;
        if (!system_content.empty()) {
            full_prompt = context_ability + "[角色设定]\n" + system_content + 
                "\n\n[用户消息]\n" + user_content;
        } else {
            full_prompt = context_ability + user_content;
        }
        
        LOG_INFO("[AI] Phase1 prompt length: " + std::to_string(full_prompt.length()));
        
        std::string current_prompt = full_prompt;
        std::string response;
        std::string tool_history;
        int max_iterations = 5;
        std::set<std::string> executed_tools;
        
        while (max_iterations > 0) {
            max_iterations--;
            
            try {
                response = callApi(current_prompt);
            } catch (...) {
                LOG_ERROR("[AI] API call failed in tool loop");
                break;
            }
            
            if (response.empty()) {
                LOG_WARN("[AI] Empty response in tool loop");
                break;
            }
            
            std::string thinking = extractThinking(response);
            std::vector<std::pair<std::string, std::string>> tool_calls;
            if (!thinking.empty()) {
                LOG_INFO("[AI] THINK block: " + thinking.substr(0, 200));
                tool_calls = parseToolCalls(thinking);
            } else {
                tool_calls = parseToolCalls(response);
            }
            if (tool_calls.empty()) {
                break;
            }
            std::vector<std::pair<std::string, std::string>> new_calls;
            for (const auto& tc : tool_calls) {
                std::string key = tc.first + "=" + tc.second;
                if (executed_tools.find(key) == executed_tools.end()) {
                    new_calls.push_back(tc);
                } else {
                    LOG_INFO("[AI] Skipping duplicate tool call: " + key);
                }
            }
            
            if (new_calls.empty()) {
                LOG_INFO("[AI] All tool calls are duplicates, ending loop");
                break;
            }
            
            LOG_INFO("[AI] Detected " + std::to_string(new_calls.size()) + " new tool call(s), iteration " + std::to_string(5 - max_iterations));
            
            std::string tool_results;
            for (const auto& [tool_type, tool_arg] : new_calls) {
                std::string result = executeToolCall(context_key, tool_type, tool_arg);
                executed_tools.insert(tool_type + "=" + tool_arg);
                if (!result.empty()) {
                    tool_results += "[" + tool_type + "=" + tool_arg + "] 结果:\n" + result + "\n\n";
                }
            }
            
            if (tool_results.empty()) {
                LOG_INFO("[AI] No tool results, ending loop");
                break;
            }
            
            tool_history += tool_results;
            
            current_prompt = full_prompt + "\n\n[工具执行结果]\n" + tool_history + 
                "[指令]工具已返回结果。如需基于结果执行新的操作(如setcard),可在[THINK]中继续调用[QUERY:...]。"
                "禁止重复调用已执行过的相同工具。若不需要更多操作,请直接在[ANSWER]中回答用户。";
            
            LOG_INFO("[AI] Tool loop iteration, new prompt length: " + std::to_string(current_prompt.length()));
        }
        
        if (max_iterations == 0) {
            LOG_WARN("[AI] Tool loop reached max iterations");
        }
        
        Statistics::instance().recordApiCall(group_id);
        
        std::string original_response = response;
        tryAutoSetcard(original_response, context_key, executed_tools);
        
        std::string answer = extractAnswer(response);
        if (!answer.empty()) {
            response = answer;
            LOG_INFO("[AI] Extracted ANSWER block, length: " + std::to_string(response.length()));
        } else {
            std::string stripped = stripThinkBlock(response);
            stripped = stripToolCalls(stripped);
            size_t check = stripped.find_first_not_of(" \n\r\t");
            if (check == std::string::npos) {
                LOG_WARN("[AI] Empty after stripping, retrying with simplified prompt");
                std::string retry_prompt = full_prompt + "\n\n[系统提示]请直接用[ANSWER]...[/ANSWER]格式回答用户。如需修改名片必须在[THINK]中调用[QUERY:setcard=QQ号,新名片]。";
                try {
                    std::string retry_resp = callApi(retry_prompt);
                    std::string retry_answer = extractAnswer(retry_resp);
                    if (!retry_answer.empty()) {
                        response = retry_answer;
                        LOG_INFO("[AI] Retry extracted ANSWER, length: " + std::to_string(response.length()));
                    } else {
                        response = stripThinkBlock(retry_resp);
                        response = stripToolCalls(response);
                        LOG_INFO("[AI] Retry fallback response");
                    }
                } catch (...) {
                    response = stripped;
                    LOG_ERROR("[AI] Retry API call failed");
                }
            } else {
                response = stripped;
                LOG_INFO("[AI] No ANSWER block, using fallback response");
            }
        }
        
        response = personality.sanitizeOutput(response);
        
        size_t start = response.find_first_not_of(" \n\r\t");
        if (start != std::string::npos) {
            response = response.substr(start);
        }
        size_t end = response.find_last_not_of(" \n\r\t");
        if (end != std::string::npos) {
            response = response.substr(0, end + 1);
        }
        
        if (!context_key.empty() && !response.empty()) {
            std::string ai_name = group_id > 0 ? personality.getNameForGroup(group_id) : personality.getCurrentName();
            db.addMessage(context_key, "assistant", response, ai_name, 0);
        }
        
        return response;
    }
    
    std::vector<std::pair<std::string, std::string>> parseToolCalls(const std::string& response) {
        std::vector<std::pair<std::string, std::string>> tools;
        static const std::vector<std::string> placeholder_args = {
            "节日名", "关键词", "用户名", "数量", "年份", "QQ号,新名片", "QQ号,专属头衔",
            "...", "xxx", "XXX", "N", "tool", "arg", "类型", "参数",
            "类型=参数", "日期", "YYYY-MM-DD", "M月D日"
        };
        static const std::vector<std::string> valid_types = {
            "holiday", "keyword", "sender", "recent", "users", "summary", "setcard", "settitle", "date"
        };
        std::string text = response;
        size_t pos = 0;
        
        while ((pos = text.find("[QUERY:", pos)) != std::string::npos) {
            size_t end = text.find("]", pos);
            if (end == std::string::npos) break;
            
            std::string query_str = text.substr(pos + 7, end - pos - 7);
            size_t eq_pos = query_str.find("=");
            std::string tool_type, tool_arg;
            if (eq_pos != std::string::npos) {
                tool_type = query_str.substr(0, eq_pos);
                tool_arg = query_str.substr(eq_pos + 1);
            } else if (!query_str.empty()) {
                tool_type = query_str;
            }
            
            bool valid = !tool_type.empty();
            if (valid) {
                bool known = false;
                for (const auto& vt : valid_types) {
                    if (tool_type == vt) { known = true; break; }
                }
                if (!known) valid = false;
            }
            if (valid && tool_type != "users") {
                if (tool_arg.empty() && tool_type != "settitle") {
                    valid = false;
                } else if (!tool_arg.empty()) {
                    for (const auto& ph : placeholder_args) {
                        if (tool_arg == ph) { valid = false; break; }
                    }
                }
            }
            
            if (valid) {
                tools.push_back({tool_type, tool_arg});
            } else if (!tool_type.empty()) {
                LOG_INFO("[AI] Rejected invalid tool call: " + tool_type + "=" + tool_arg);
            }
            pos = end + 1;
        }
        return tools;
    }
    
    std::string executeToolCall(const std::string& context_key, const std::string& tool_type, const std::string& tool_arg) {
        LOG_INFO("[AI] Executing tool: " + tool_type + "=" + tool_arg);
        
        if (tool_type == "holiday") {
            return Calendar::instance().queryHoliday(tool_arg);
        } else if (tool_type == "keyword" && !context_key.empty()) {
            return ContextDatabase::instance().queryByKeyword(context_key, tool_arg, 15);
        } else if (tool_type == "sender" && !context_key.empty()) {
            return ContextDatabase::instance().queryBySender(context_key, tool_arg, 15);
        } else if (tool_type == "recent" && !context_key.empty()) {
            try {
                int count = std::stoi(tool_arg);
                if (count > 50) count = 50;
                return ContextDatabase::instance().queryRecent(context_key, count);
            } catch (...) {
                return "参数错误: 需要数字";
            }
        } else if (tool_type == "users" && !context_key.empty()) {
            return ContextDatabase::instance().queryActiveUsers(context_key, 20);
        } else if (tool_type == "summary" && !context_key.empty()) {
            int year = 0;
            if (!tool_arg.empty()) {
                try { year = std::stoi(tool_arg); } catch (...) { year = 0; }
            }
            return ContextDatabase::instance().queryYearSummary(context_key, year);
        } else if (tool_type == "date" && !context_key.empty()) {
            return ContextDatabase::instance().queryByDate(context_key, tool_arg);
        } else if (tool_type == "setcard" && !context_key.empty()) {
            if (!set_card_func_) return "setcard工具未初始化";
            size_t comma = tool_arg.find(",");
            if (comma == std::string::npos) return "参数格式错误,需要: QQ号,新名片";
            std::string uid_str = tool_arg.substr(0, comma);
            std::string card = tool_arg.substr(comma + 1);
            int64_t target_uid = 0;
            try { target_uid = std::stoll(uid_str); } catch (...) {
                target_uid = ContextDatabase::instance().findUserIdByName(context_key, uid_str);
            }
            if (target_uid == 0) return "找不到用户: " + uid_str;
            int64_t group_id = 0;
            if (context_key.substr(0, 2) == "g_") {
                try { group_id = std::stoll(context_key.substr(2)); } catch (...) {}
            }
            if (group_id == 0) return "仅群聊可用";
            if (!GroupMemberCache::instance().isMember(group_id, target_uid))
                return "错误: QQ " + std::to_string(target_uid) + " 不是本群成员,请从[本群成员列表]或[CQ:at]中获取正确的QQ号";
            set_card_func_(group_id, target_uid, card);
            return "已将用户" + std::to_string(target_uid) + "的群名片设置为: " + card;
        } else if (tool_type == "settitle" && !context_key.empty()) {
            if (!set_title_func_) return "settitle工具未初始化";
            size_t comma = tool_arg.find(",");
            std::string uid_str, title;
            if (comma == std::string::npos) {
                uid_str = tool_arg;
                title = "";
            } else {
                uid_str = tool_arg.substr(0, comma);
                title = tool_arg.substr(comma + 1);
            }
            int64_t target_uid = 0;
            try { target_uid = std::stoll(uid_str); } catch (...) {
                target_uid = ContextDatabase::instance().findUserIdByName(context_key, uid_str);
            }
            if (target_uid == 0) return "找不到用户: " + uid_str;
            int64_t group_id = 0;
            if (context_key.substr(0, 2) == "g_") {
                try { group_id = std::stoll(context_key.substr(2)); } catch (...) {}
            }
            if (group_id == 0) return "仅群聊可用";
            if (!GroupMemberCache::instance().isMember(group_id, target_uid))
                return "错误: QQ " + std::to_string(target_uid) + " 不是本群成员,请从[本群成员列表]或[CQ:at]中获取正确的QQ号";
            set_title_func_(group_id, target_uid, title);
            if (title.empty()) return "已删除用户" + std::to_string(target_uid) + "的专属头衔";
            return "已将用户" + std::to_string(target_uid) + "的专属头衔设置为: " + title;
        }
        return "未知工具: " + tool_type;
    }
    
    std::string extractCardNameFromText(const std::string& text) {
        struct Pair { const char* open; size_t olen; const char* close; size_t clen; };
        Pair brackets[] = {
            {"\xe3\x80\x8c", 3, "\xe3\x80\x8d", 3},
            {"\xe2\x80\x9c", 3, "\xe2\x80\x9d", 3},
        };
        for (const auto& bp : brackets) {
            size_t p = text.find(bp.open);
            while (p != std::string::npos) {
                size_t e = text.find(bp.close, p + bp.olen);
                if (e != std::string::npos) {
                    std::string c = text.substr(p + bp.olen, e - p - bp.olen);
                    if (c.size() >= 1 && c.size() <= 60) {
                        size_t ctx = (p > 80) ? p - 80 : 0;
                        std::string before = text.substr(ctx, p - ctx);
                        if (before.find("\xe5\x90\x8d\xe7\x89\x87") != std::string::npos ||
                            before.find("\xe6\x94\xb9\xe5\x90\x8d") != std::string::npos ||
                            before.find("\xe6\x94\xb9\xe4\xb8\xba") != std::string::npos ||
                            before.find("\xe6\x94\xb9\xe6\x88\x90") != std::string::npos) {
                            return c;
                        }
                    }
                }
                p = text.find(bp.open, p + bp.olen);
            }
        }
        for (const auto& bp : brackets) {
            size_t p = text.find(bp.open);
            if (p != std::string::npos) {
                size_t e = text.find(bp.close, p + bp.olen);
                if (e != std::string::npos) {
                    std::string c = text.substr(p + bp.olen, e - p - bp.olen);
                    if (c.size() >= 1 && c.size() <= 60) return c;
                }
            }
        }
        const char* seps[] = {
            "\xe6\x94\xb9\xe4\xb8\xba", "\xe6\x94\xb9\xe6\x88\x90",
            "\xe8\xae\xbe\xe4\xb8\xba", "\xe8\xae\xbe\xe7\xbd\xae\xe4\xb8\xba",
            "\xe5\x8f\x98\xe6\x88\x90",
            "\xef\xbc\x9a", ":"
        };
        std::string mp = "\xe5\x90\x8d\xe7\x89\x87";
        size_t mp_pos = text.find(mp);
        while (mp_pos != std::string::npos) {
            for (const char* sep : seps) {
                size_t sp = text.find(sep, mp_pos + mp.size());
                if (sp != std::string::npos && sp - mp_pos < 30) {
                    size_t vs = sp + strlen(sep);
                    while (vs < text.size() && (text[vs] == ' ' || text[vs] == '\t')) vs++;
                    size_t ve = vs;
                    while (ve < text.size() && text[ve] != '\n' && text[ve] != '\r') ve++;
                    if (ve > vs) {
                        std::string c = text.substr(vs, ve - vs);
                        while (!c.empty() && (c.back() == ' ' || c.back() == '\t')) c.pop_back();
                        if (c.size() >= 1 && c.size() <= 60) return c;
                    }
                }
            }
            mp_pos = text.find(mp, mp_pos + mp.size());
        }
        return "";
    }

    void tryAutoSetcard(const std::string& full_response, const std::string& context_key,
                        const std::set<std::string>& executed_tools) {
        if (!executed_tools.empty()) return;
        if (context_key.size() < 3 || context_key.substr(0, 2) != "g_") return;
        if (!set_card_func_) return;

        std::string think = extractThinking(full_response);
        std::string answer = extractAnswer(full_response);
        if (answer.empty()) answer = stripThinkBlock(full_response);
        std::string combined = think + "\n" + answer;

        if (combined.find("\xe5\x90\x8d\xe7\x89\x87") == std::string::npos) return;

        std::vector<int64_t> qqs;
        std::set<int64_t> seen;
        for (size_t i = 0; i < combined.size(); ) {
            if (combined[i] >= '0' && combined[i] <= '9') {
                size_t s = i;
                while (i < combined.size() && combined[i] >= '0' && combined[i] <= '9') i++;
                if (i - s >= 5 && i - s <= 12) {
                    try {
                        int64_t q = std::stoll(combined.substr(s, i - s));
                        if (seen.insert(q).second) qqs.push_back(q);
                    } catch (...) {}
                }
            } else { i++; }
        }

        int64_t target = 0;
        if (qqs.size() >= 2) target = qqs[1];
        else if (qqs.size() == 1) target = qqs[0];
        if (target == 0) return;

        std::string card = extractCardNameFromText(combined);
        if (card.empty()) return;
        if (card.find("QUERY") != std::string::npos || card.find("[") != std::string::npos) return;
        if (card.size() <= 3) return;
        bool has_alnum = false;
        for (unsigned char c : card) { if (c >= 0x80 || std::isalnum(c)) { has_alnum = true; break; } }
        if (!has_alnum) return;

        int64_t group_id = 0;
        try { group_id = std::stoll(context_key.substr(2)); } catch (...) { return; }

        LOG_INFO("[AI] Auto-compensating setcard: group=" + std::to_string(group_id) +
                 " target=" + std::to_string(target) + " card=" + card);
        set_card_func_(group_id, target, card);
    }

    std::string stripToolCalls(const std::string& response) {
        std::string result = response;
        size_t pos = 0;
        while ((pos = result.find("[QUERY:")) != std::string::npos) {
            size_t end = result.find("]", pos);
            if (end != std::string::npos) {
                result = result.substr(0, pos) + result.substr(end + 1);
            } else {
                break;
            }
        }
        return result;
    }
    
    std::string extractBlock(const std::string& text, const std::string& open_tag, const std::string& close_tag) {
        size_t start = text.find(open_tag);
        if (start == std::string::npos) return "";
        start += open_tag.length();
        size_t end = text.find(close_tag, start);
        if (end == std::string::npos) return text.substr(start);
        return text.substr(start, end - start);
    }
    
    std::string extractThinking(const std::string& response) {
        return extractBlock(response, "[THINK]", "[/THINK]");
    }
    
    std::string extractAnswer(const std::string& response) {
        return extractBlock(response, "[ANSWER]", "[/ANSWER]");
    }
    
    std::string stripThinkBlock(const std::string& text) {
        std::string result = text;
        size_t think_start = result.find("[THINK]");
        while (think_start != std::string::npos) {
            size_t think_end = result.find("[/THINK]", think_start);
            if (think_end != std::string::npos) {
                result = result.substr(0, think_start) + result.substr(think_end + 8);
            } else {
                result = result.substr(0, think_start);
            }
            think_start = result.find("[THINK]");
        }
        return result;
    }
    
    
    void clearContext(int64_t group_id = 0, int64_t user_id = 0) {
        std::string context_key;
        if (group_id > 0) {
            context_key = "g_" + std::to_string(group_id);
        } else if (user_id > 0) {
            context_key = "p_" + std::to_string(user_id);
        }
        
        if (!context_key.empty()) {
            ContextDatabase::instance().clearContext(context_key);
        }
    }
    
    std::string chatWithImages(const std::string& message, const std::vector<ImageData>& images,
                                int64_t group_id = 0, int64_t user_id = 0,
                                const std::string& sender_name = "") {
        auto& personality = PersonalitySystem::instance();
        std::string sanitized_message = personality.sanitizeInput(message);
        
        std::string personality_prompt;
        if (group_id > 0) {
            personality_prompt = personality.getPromptForGroup(group_id);
        } else {
            personality_prompt = personality.getCurrentPrompt();
        }
        
        std::string full_prompt;
        if (!personality_prompt.empty()) {
            full_prompt = "[角色设定]\n" + personality_prompt + "\n\n";
        }
        
        if (!sender_name.empty()) {
            full_prompt += sender_name + ": " + sanitized_message;
        } else {
            full_prompt += sanitized_message;
        }
        
        if (images.size() > 1) {
            full_prompt += "\n[包含" + std::to_string(images.size()) + "张图片]";
        }
        
        LOG_INFO("[AI] Chat with " + std::to_string(images.size()) + " images");
        return callApiWithImages(full_prompt, images);
    }
    
    std::string chatWithoutContext(const std::string& message) {
        std::string full_prompt;
        if (!system_prompt_.empty()) {
            full_prompt = system_prompt_ + "\n\n";
        }
        full_prompt += message;
        
        return callApi(full_prompt);
    }
    
    GeneratedImage generateImage(const std::string& prompt, const std::vector<ImageData>& source_images = {}) {
        GeneratedImage result;
        std::string response;
        
        if (source_images.empty()) {
            std::string format = getRequestFormat();
            if (format == "messages") {
                std::string post_data = "{\"model\":\"" + escapeJson(current_model_) + "\",";
                post_data += "\"max_tokens\":4096,";
                post_data += "\"stream\":true,";
                post_data += "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}]}";
                response = callApiStreamRaw(post_data, "application/json; charset=UTF-8");
            } else {
                response = callApi(prompt);
            }
        } else {
            response = callApiWithImagesStream(prompt, source_images);
        }
        
        if (response.empty()) return result;
        
        result = parseImageResponse(response);
        return result;
    }
    
    std::string saveBase64Image(const std::string& base64_data, const std::string& media_type) {
#ifdef _WIN32
        std::string ext = "jpg";
        if (media_type.find("png") != std::string::npos) ext = "png";
        else if (media_type.find("gif") != std::string::npos) ext = "gif";
        else if (media_type.find("webp") != std::string::npos) ext = "webp";
        
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string filename = "data/generated_" + std::to_string(ms) + "." + ext;
        
        std::vector<unsigned char> decoded;
        static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string clean_data;
        for (char c : base64_data) {
            if (base64_chars.find(c) != std::string::npos || c == '=') {
                clean_data += c;
            }
        }
        
        size_t in_len = clean_data.size();
        if (in_len % 4 != 0) return "";
        
        size_t out_len = in_len / 4 * 3;
        if (clean_data[in_len - 1] == '=') out_len--;
        if (clean_data[in_len - 2] == '=') out_len--;
        
        decoded.resize(out_len);
        
        for (size_t i = 0, j = 0; i < in_len;) {
            uint32_t a = clean_data[i] == '=' ? 0 : base64_chars.find(clean_data[i]); i++;
            uint32_t b = clean_data[i] == '=' ? 0 : base64_chars.find(clean_data[i]); i++;
            uint32_t c = clean_data[i] == '=' ? 0 : base64_chars.find(clean_data[i]); i++;
            uint32_t d = clean_data[i] == '=' ? 0 : base64_chars.find(clean_data[i]); i++;
            
            uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
            
            if (j < out_len) decoded[j++] = (triple >> 16) & 0xFF;
            if (j < out_len) decoded[j++] = (triple >> 8) & 0xFF;
            if (j < out_len) decoded[j++] = triple & 0xFF;
        }
        
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            LOG_ERROR("[AI] Failed to save image: " + filename);
            return "";
        }
        file.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        file.close();
        
        LOG_INFO("[AI] Saved generated image: " + filename + " (" + std::to_string(decoded.size()) + " bytes)");
        return filename;
#else
        return "";
#endif
    }
    
private:
    AIService() {
        api_url_ = "";
        system_prompt_ = "";
    }
    
    std::string urlEncode(const std::string& str) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        
        for (unsigned char c : str) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else if (c == ' ') {
                escaped << '+';
            } else {
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int(c);
                escaped << std::nouppercase;
            }
        }
        
        return escaped.str();
    }
    
    std::string truncateUtf8(const std::string& str, size_t max_bytes) {
        if (str.length() <= max_bytes) return str;
        
        size_t pos = max_bytes;
        while (pos > 0 && (str[pos] & 0xC0) == 0x80) {
            pos--;
        }
        return str.substr(0, pos);
    }
    
    std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    std::string getRequestFormat() const {
        if (models_.count(current_model_)) {
            return models_.at(current_model_).format;
        }
        return "json";
    }
    
    std::string getEffectiveApiKey() const {
        if (models_.count(current_model_)) {
            const auto& cfg = models_.at(current_model_);
            if (!cfg.api_key.empty()) return cfg.api_key;
        }
        return api_key_;
    }
    
    std::string getModelName() const {
        if (models_.count(current_model_)) {
            const auto& cfg = models_.at(current_model_);
            if (!cfg.model_name.empty()) return cfg.model_name;
        }
        return current_model_;
    }
    
    std::wstring buildAuthHeader() const {
        std::string key = getEffectiveApiKey();
        if (key.empty()) return L"";
        std::string format = getRequestFormat();
        if (format == "openai") {
            return L"Authorization: Bearer " + std::wstring(key.begin(), key.end()) + L"\r\n";
        } else {
            return L"x-goog-api-key: " + std::wstring(key.begin(), key.end()) + L"\r\n";
        }
    }
    
public:
    std::string downloadImageAsBase64(const std::string& url) {
#ifdef _WIN32
        std::string image_url = url;
        if (image_url.find("&amp;") != std::string::npos) {
            size_t pos = 0;
            while ((pos = image_url.find("&amp;", pos)) != std::string::npos) {
                image_url.replace(pos, 5, "&");
                pos += 1;
            }
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, image_url.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, image_url.c_str(), -1, &wUrl[0], wlen);
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            LOG_ERROR("[AI] Failed to parse image URL");
            return "";
        }
        
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";
        
        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.dwExtraInfoLength > 0) {
            urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
        
        DWORD timeout = 30000;
        WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
            !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return "";
        }
        
        std::vector<unsigned char> imageData;
        DWORD dwSize = 0, dwDownloaded = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
            std::vector<unsigned char> buffer(dwSize);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
            imageData.insert(imageData.end(), buffer.begin(), buffer.begin() + dwDownloaded);
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        if (imageData.empty()) return "";
        
        static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve(((imageData.size() + 2) / 3) * 4);
        
        for (size_t i = 0; i < imageData.size(); i += 3) {
            unsigned int n = imageData[i] << 16;
            if (i + 1 < imageData.size()) n |= imageData[i + 1] << 8;
            if (i + 2 < imageData.size()) n |= imageData[i + 2];
            
            encoded += base64_chars[(n >> 18) & 0x3F];
            encoded += base64_chars[(n >> 12) & 0x3F];
            encoded += (i + 1 < imageData.size()) ? base64_chars[(n >> 6) & 0x3F] : '=';
            encoded += (i + 2 < imageData.size()) ? base64_chars[n & 0x3F] : '=';
        }
        
        LOG_INFO("[AI] Downloaded image: " + std::to_string(imageData.size()) + " bytes -> base64: " + std::to_string(encoded.size()));
        return encoded;
#else
        return "";
#endif
    }
    
    std::string downloadAndSaveImage(const std::string& url) {
        std::string base64 = downloadImageAsBase64(url);
        if (base64.empty()) return "";
        std::string media_type = "image/jpeg";
        if (url.find(".png") != std::string::npos) media_type = "image/png";
        return saveBase64Image(base64, media_type);
    }
    
    std::string callApiWithImages(const std::string& prompt, const std::vector<ImageData>& images) {
#ifdef _WIN32
        std::string format = getRequestFormat();
        if ((format != "messages" && format != "openai") || images.empty()) {
            return callApi(prompt);
        }
        
        std::string mn = getModelName();
        
        if (format == "openai") {
            std::string post_data = "{\"model\":\"" + escapeJson(mn) + "\",";
            post_data += "\"messages\":[{\"role\":\"user\",\"content\":[";
            post_data += "{\"type\":\"text\",\"text\":\"" + escapeJson(prompt) + "\"}";
            for (const auto& img : images) {
                if (!img.base64.empty()) {
                    post_data += ",{\"type\":\"image_url\",\"image_url\":{";
                    post_data += "\"url\":\"data:" + img.media_type + ";base64," + img.base64 + "\"}}";
                } else if (!img.url.empty()) {
                    post_data += ",{\"type\":\"image_url\",\"image_url\":{";
                    post_data += "\"url\":\"" + escapeJson(img.url) + "\"}}";
                }
            }
            post_data += "]}],\"stream\":false}";
            return callApiRaw(post_data, "application/json; charset=UTF-8");
        }
        
        std::string post_data = "{\"model\":\"" + escapeJson(mn) + "\",";
        post_data += "\"max_tokens\":4096,";
        post_data += "\"messages\":[{\"role\":\"user\",\"content\":[";
        post_data += "{\"type\":\"text\",\"text\":\"" + escapeJson(prompt) + "\"}";
        
        for (const auto& img : images) {
            if (!img.base64.empty()) {
                post_data += ",{\"type\":\"image\",\"source\":{";
                post_data += "\"type\":\"base64\",";
                post_data += "\"media_type\":\"" + escapeJson(img.media_type) + "\",";
                post_data += "\"data\":\"" + img.base64 + "\"}}";
            } else if (!img.url.empty()) {
                post_data += ",{\"type\":\"image\",\"source\":{";
                post_data += "\"type\":\"url\",";
                post_data += "\"url\":\"" + escapeJson(img.url) + "\"}}";
            }
        }
        
        post_data += "]}]}";
        
        return callApiRaw(post_data, "application/json; charset=UTF-8");
#else
        return "";
#endif
    }
    
    std::string callApiRaw(const std::string& post_data, const std::string& content_type) {
#ifdef _WIN32
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            LOG_ERROR("[AI] WinHttpOpen failed");
            return "";
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, &wUrl[0], wlen);
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
        
        DWORD timeout_connect = 60000, timeout_send = 120000, timeout_receive = 300000;
        WinHttpSetTimeouts(hRequest, timeout_connect, timeout_connect, timeout_send, timeout_receive);
        
        std::wstring header_str = L"Content-Type: " + std::wstring(content_type.begin(), content_type.end()) + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, header_str.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        
        {
            std::wstring auth_hdr = buildAuthHeader();
            if (!auth_hdr.empty()) {
                WinHttpAddRequestHeaders(hRequest, auth_hdr.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }
        }
        
        LOG_INFO("[AI] POST data length: " + std::to_string(post_data.length()));
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)post_data.c_str(), (DWORD)post_data.length(), (DWORD)post_data.length(), 0)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return "";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD error = GetLastError();
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            last_error_ = ErrorCode::AI_API_ERROR;
            return "";
        }
        
        std::string response;
        DWORD dwSize = 0, dwDownloaded = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
            std::vector<char> buffer(dwSize + 1);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
            buffer[dwDownloaded] = '\0';
            response += buffer.data();
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        LOG_INFO("[AI] API response length: " + std::to_string(response.length()));
        
        return parseApiResponse(response);
#else
        return "";
#endif
    }
    
    std::string callApiStreamRaw(const std::string& post_data, const std::string& content_type) {
#ifdef _WIN32
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            LOG_ERROR("[AI] WinHttpOpen failed");
            return "";
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, &wUrl[0], wlen);
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
        
        DWORD timeout_connect = 60000, timeout_send = 120000, timeout_receive = 600000;
        WinHttpSetTimeouts(hRequest, timeout_connect, timeout_connect, timeout_send, timeout_receive);
        
        std::wstring header_str = L"Content-Type: " + std::wstring(content_type.begin(), content_type.end()) + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, header_str.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        
        {
            std::wstring auth_hdr = buildAuthHeader();
            if (!auth_hdr.empty()) {
                WinHttpAddRequestHeaders(hRequest, auth_hdr.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }
        }
        
        LOG_INFO("[AI] Stream POST data length: " + std::to_string(post_data.length()));
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)post_data.c_str(), (DWORD)post_data.length(), (DWORD)post_data.length(), 0)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return "";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            last_error_ = ErrorCode::AI_API_ERROR;
            return "";
        }
        
        std::string full_response;
        std::string text_content;
        std::string image_base64;
        std::string image_media_type;
        
        DWORD dwSize = 0, dwDownloaded = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
            std::vector<char> buffer(dwSize + 1);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
            buffer[dwDownloaded] = '\0';
            full_response += buffer.data();
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        std::string merged_response = parseStreamResponse(full_response);
        LOG_INFO("[AI] Stream merged response length: " + std::to_string(merged_response.length()));
        
        return merged_response;
#else
        return "";
#endif
    }
    
    std::string callApiWithImagesStream(const std::string& prompt, const std::vector<ImageData>& images) {
#ifdef _WIN32
        std::string format = getRequestFormat();
        if (format != "messages" || images.empty()) {
            return callApi(prompt);
        }
        
        std::string post_data = "{\"model\":\"" + escapeJson(current_model_) + "\",";
        post_data += "\"max_tokens\":4096,";
        post_data += "\"stream\":true,";
        post_data += "\"messages\":[{\"role\":\"user\",\"content\":[";
        post_data += "{\"type\":\"text\",\"text\":\"" + escapeJson(prompt) + "\"}";
        
        for (const auto& img : images) {
            if (!img.base64.empty()) {
                post_data += ",{\"type\":\"image\",\"source\":{";
                post_data += "\"type\":\"base64\",";
                post_data += "\"media_type\":\"" + escapeJson(img.media_type) + "\",";
                post_data += "\"data\":\"" + img.base64 + "\"}}";
            } else if (!img.url.empty()) {
                post_data += ",{\"type\":\"image\",\"source\":{";
                post_data += "\"type\":\"url\",";
                post_data += "\"url\":\"" + escapeJson(img.url) + "\"}}";
            }
        }
        
        post_data += "]}]}";
        
        return callApiStreamRaw(post_data, "application/json; charset=UTF-8");
#else
        return "";
#endif
    }
    
    std::string parseStreamResponse(const std::string& stream_data) {
        std::string result_json = "{\"content\":[";
        bool first_block = true;
        
        std::istringstream stream(stream_data);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.empty() || line[0] != 'd') continue;
            if (line.find("data: ") != 0) continue;
            
            std::string json_str = line.substr(6);
            if (json_str == "[DONE]") break;
            
            if (json_str.find("\"type\":\"content_block_delta\"") != std::string::npos) {
                size_t delta_pos = json_str.find("\"delta\":");
                if (delta_pos == std::string::npos) continue;
                
                size_t type_pos = json_str.find("\"type\":\"", delta_pos);
                if (type_pos == std::string::npos) continue;
                
                size_t type_start = type_pos + 8;
                size_t type_end = json_str.find("\"", type_start);
                std::string block_type = json_str.substr(type_start, type_end - type_start);
                
                if (block_type == "text_delta") {
                    size_t text_pos = json_str.find("\"text\":\"", delta_pos);
                    if (text_pos != std::string::npos) {
                        size_t text_start = text_pos + 8;
                        size_t text_end = text_start;
                        while (text_end < json_str.length()) {
                            if (json_str[text_end] == '\"' && json_str[text_end-1] != '\\') break;
                            text_end++;
                        }
                        std::string text = json_str.substr(text_start, text_end - text_start);
                        if (!first_block) result_json += ",";
                        result_json += "{\"type\":\"text\",\"text\":\"" + text + "\"}";
                        first_block = false;
                    }
                } else if (block_type == "image_delta") {
                    size_t data_pos = json_str.find("\"data\":\"", delta_pos);
                    size_t media_pos = json_str.find("\"media_type\":\"", delta_pos);
                    if (data_pos != std::string::npos) {
                        size_t data_start = data_pos + 8;
                        size_t data_end = data_start;
                        while (data_end < json_str.length() && json_str[data_end] != '\"') data_end++;
                        std::string data = json_str.substr(data_start, data_end - data_start);
                        
                        std::string media_type = "image/png";
                        if (media_pos != std::string::npos) {
                            size_t m_start = media_pos + 14;
                            size_t m_end = json_str.find("\"", m_start);
                            media_type = json_str.substr(m_start, m_end - m_start);
                        }
                        
                        if (!first_block) result_json += ",";
                        result_json += "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"" + media_type + "\",\"data\":\"" + data + "\"}}";
                        first_block = false;
                    }
                }
            }
        }
        
        result_json += "]}";
        return result_json;
    }
    
    std::string parseApiResponse(const std::string& response) {
        if (response.empty()) return "";
        
        auto decodeUnicode = [](const std::string& input) -> std::string {
            std::string result;
            size_t i = 0;
            while (i < input.length()) {
                if (i + 5 < input.length() && input[i] == '\\' && input[i+1] == 'u') {
                    std::string hex = input.substr(i + 2, 4);
                    bool valid = true;
                    for (char c : hex) {
                        if (!std::isxdigit(c)) { valid = false; break; }
                    }
                    if (valid) {
                        uint32_t codepoint = std::stoul(hex, nullptr, 16);
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 11 < input.length() &&
                            input[i+6] == '\\' && input[i+7] == 'u') {
                            std::string hex2 = input.substr(i + 8, 4);
                            bool valid2 = true;
                            for (char c : hex2) { if (!std::isxdigit(c)) { valid2 = false; break; } }
                            if (valid2) {
                                uint32_t low = std::stoul(hex2, nullptr, 16);
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                    i += 6;
                                }
                            }
                        }
                        if (codepoint < 0x80) {
                            result += (char)codepoint;
                        } else if (codepoint < 0x800) {
                            result += (char)(0xC0 | (codepoint >> 6));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        } else if (codepoint < 0x10000) {
                            result += (char)(0xE0 | (codepoint >> 12));
                            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            result += (char)(0xF0 | (codepoint >> 18));
                            result += (char)(0x80 | ((codepoint >> 12) & 0x3F));
                            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        }
                        i += 6;
                        continue;
                    }
                }
                result += input[i++];
            }
            return result;
        };
        
        auto extractJsonField = [&decodeUnicode](const std::string& json, const std::string& field) -> std::string {
            std::string key = "\"" + field + "\":\"";
            size_t start = json.find(key);
            if (start == std::string::npos) return "";
            start += key.length();
            size_t end = start;
            while (end < json.length()) {
                if (json[end] == '\"' && json[end-1] != '\\') break;
                end++;
            }
            std::string value = json.substr(start, end - start);
            size_t pos = 0;
            while ((pos = value.find("\\n", pos)) != std::string::npos) {
                value.replace(pos, 2, "\n"); pos += 1;
            }
            pos = 0;
            while ((pos = value.find("\\\"", pos)) != std::string::npos) {
                value.replace(pos, 2, "\""); pos += 1;
            }
            return decodeUnicode(value);
        };
        
        if (response.find("{\"success\"") != std::string::npos) {
            std::string content = extractJsonField(response, "content");
            if (!content.empty()) return content;
        }
        
        if (response.find("\"status\":\"success\"") != std::string::npos && response.find("\"data\"") != std::string::npos) {
            std::string text = extractJsonField(response, "text");
            grok_last_images_.clear();
            size_t images_pos = response.find("\"images\":[");
            if (images_pos != std::string::npos) {
                size_t arr_start = images_pos + 10;
                size_t arr_end = response.find("]", arr_start);
                if (arr_end != std::string::npos) {
                    std::string arr = response.substr(arr_start, arr_end - arr_start);
                    size_t pos = 0;
                    while ((pos = arr.find("\"", pos)) != std::string::npos) {
                        size_t url_start = pos + 1;
                        size_t url_end = arr.find("\"", url_start);
                        if (url_end != std::string::npos) {
                            grok_last_images_.push_back(arr.substr(url_start, url_end - url_start));
                            pos = url_end + 1;
                        } else break;
                    }
                }
            }
            size_t conv_pos = response.find("\"conversation\":{");
            if (conv_pos != std::string::npos) {
                size_t id_pos = response.find("\"id\":\"", conv_pos);
                if (id_pos != std::string::npos) {
                    size_t id_start = id_pos + 6;
                    size_t id_end = response.find("\"", id_start);
                    if (id_end != std::string::npos) {
                        grok_last_conversation_id_ = response.substr(id_start, id_end - id_start);
                    }
                }
            }
            LOG_INFO("[AI] Grok parsed: text=" + std::to_string(text.length()) + " images=" + std::to_string(grok_last_images_.size()));
            if (!text.empty()) return text;
            if (!grok_last_images_.empty()) return "[AI生成了图片]";
            return "";
        }
        
        if (response.find("\"choices\"") != std::string::npos && response.find("\"message\"") != std::string::npos) {
            std::string content = extractJsonField(response, "content");
            if (!content.empty()) {
                LOG_INFO("[AI] OpenAI format parsed, content length: " + std::to_string(content.length()));
                return content;
            }
        }
        
        if (response.find("\"content\":[") != std::string::npos) {
            std::string text = extractJsonField(response, "text");
            if (!text.empty()) return text;
        }
        
        if (response.find("\"answer\"") != std::string::npos) {
            return extractJsonField(response, "answer");
        }
        
        if (response.find("\"response\"") != std::string::npos) {
            return extractJsonField(response, "response");
        }
        
        if (response.find("\"text\"") != std::string::npos) {
            return extractJsonField(response, "text");
        }
        
        return "";
    }
    
    std::string callApi(const std::string& prompt) {
#ifdef _WIN32
        std::string post_data;
        std::string content_type;
        std::string format = getRequestFormat();
        
        if (format == "form") {
            post_data = "question=" + urlEncode(prompt) + "&type=json";
            if (!system_prompt_.empty()) {
                post_data += "&system=" + urlEncode(system_prompt_);
            }
            content_type = "application/x-www-form-urlencoded; charset=UTF-8";
        } else if (format == "openai") {
            std::string mn = getModelName();
            post_data = "{\"model\":\"" + escapeJson(mn) + "\",";
            post_data += "\"messages\":[";
            if (!system_prompt_.empty()) {
                post_data += "{\"role\":\"system\",\"content\":\"" + escapeJson(system_prompt_) + "\"},";
            }
            post_data += "{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}";
            post_data += "],\"stream\":false}";
            content_type = "application/json; charset=UTF-8";
        } else if (format == "messages") {
            post_data = "{\"model\":\"" + escapeJson(getModelName()) + "\",";
            post_data += "\"max_tokens\":4096,";
            post_data += "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}]}";
            content_type = "application/json; charset=UTF-8";
        } else if (format == "grok") {
            post_data = "{\"message\":\"" + escapeJson(prompt) + "\"}";
            content_type = "application/json; charset=UTF-8";
        } else {
            post_data = "{\"question\":\"" + escapeJson(prompt) + "\",\"type\":\"json\"";
            if (!system_prompt_.empty()) {
                post_data += ",\"system\":\"" + escapeJson(system_prompt_) + "\"";
            }
            post_data += "}";
            content_type = "application/json; charset=UTF-8";
        }
        
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            LOG_ERROR("[AI] WinHttpOpen failed");
            return "";
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, &wUrl[0], wlen);
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpCrackUrl failed: " + std::to_string(GetLastError()));
            return "";
        }
        
        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpConnect failed");
            return "";
        }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpOpenRequest failed");
            return "";
        }
        
        DWORD timeout_connect = 60000;
        DWORD timeout_send = 120000;
        DWORD timeout_receive = 300000;
        WinHttpSetTimeouts(hRequest, timeout_connect, timeout_connect, timeout_send, timeout_receive);
        
        std::wstring header_str = L"Content-Type: " + std::wstring(content_type.begin(), content_type.end()) + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, header_str.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        
        std::wstring auth_hdr = buildAuthHeader();
        if (!auth_hdr.empty()) {
            WinHttpAddRequestHeaders(hRequest, auth_hdr.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            LOG_INFO("[AI] Auth header added for format: " + format);
        } else {
            LOG_WARN("[AI] API key is empty!");
        }
        
        LOG_INFO("[AI] POST data length: " + std::to_string(post_data.length()));
        LOG_INFO("[AI] POST data start: " + post_data.substr(0, post_data.length() > 100 ? 100 : post_data.length()));
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)post_data.c_str(), (DWORD)post_data.length(), (DWORD)post_data.length(), 0)) {
            DWORD error = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpSendRequest failed, error code: " + std::to_string(error));
            return "";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD error = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            std::string err_msg = "[AI] WinHttpReceiveResponse failed, error: " + std::to_string(error);
            if (error == 12002) err_msg += " (timeout)";
            else if (error == 12029) err_msg += " (connection failed)";
            else if (error == 12030) err_msg += " (connection aborted)";
            LOG_ERROR(err_msg);
            last_error_ = ErrorCode::AI_API_ERROR;
            return "";
        }
        
        std::string response;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                break;
            }
            
            if (dwSize == 0) break;
            
            std::vector<char> buffer(dwSize + 1);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                break;
            }
            
            buffer[dwDownloaded] = '\0';
            response += buffer.data();
            
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        if (response.empty()) {
            LOG_WARN("[AI] API returned empty response");
            return "";
        }
        
        LOG_INFO("[AI] API response length: " + std::to_string(response.length()));
        size_t log_len = response.length() > 500 ? 500 : response.length();
        LOG_INFO("[AI] API response: " + response.substr(0, log_len));
        
        bool is_error = (response.find("{\"error\"") != std::string::npos) ||
            (response.find("\"type\":\"error\"") != std::string::npos) ||
            (response.find("\"error\":{\"type\"") != std::string::npos);
        if (is_error) {
            std::string error_msg;
            size_t msg_pos = response.find("\"message\":\"");
            if (msg_pos != std::string::npos) {
                size_t msg_start = msg_pos + 11;
                size_t msg_end = msg_start;
                while (msg_end < response.length() && !(response[msg_end] == '"' && response[msg_end-1] != '\\')) msg_end++;
                error_msg = response.substr(msg_start, msg_end - msg_start);
            } else {
                size_t es = response.find("\"error\":\"");
                if (es != std::string::npos) {
                    es += 9;
                    size_t ee = response.find("\"", es);
                    error_msg = (ee != std::string::npos) ? response.substr(es, ee - es) : response;
                } else {
                    error_msg = response.substr(0, 200);
                }
            }
            if (error_msg.find("RESOURCE_EXHAUSTED") != std::string::npos ||
                error_msg.find("exhausted") != std::string::npos ||
                error_msg.find("quota") != std::string::npos ||
                error_msg.find("Quota") != std::string::npos) {
                last_error_ = ErrorCode::AI_API_QUOTA_EXHAUSTED;
                size_t reset_pos = error_msg.find("reset after ");
                if (reset_pos != std::string::npos) {
                    last_error_detail_ = error_msg.substr(reset_pos + 12);
                    size_t end = last_error_detail_.find('"');
                    if (end != std::string::npos) last_error_detail_ = last_error_detail_.substr(0, end);
                }
            } else if (error_msg.find("rate") != std::string::npos || 
                error_msg.find("limit") != std::string::npos ||
                error_msg.find("耗尽") != std::string::npos ||
                error_msg.find("频率") != std::string::npos) {
                last_error_ = ErrorCode::AI_API_RATE_LIMIT;
            } else if (error_msg.find("key") != std::string::npos || 
                       error_msg.find("密钥") != std::string::npos ||
                       error_msg.find("认证") != std::string::npos) {
                last_error_ = ErrorCode::AI_API_INVALID_KEY;
            } else {
                last_error_ = ErrorCode::AI_API_ERROR;
            }
            LOG_ERROR(ErrorSystem::instance().formatError(last_error_, error_msg));
            return "";
        }
        
        auto extractJsonField = [](const std::string& json, const std::string& field) -> std::string {
            std::string key = "\"" + field + "\":\"";
            size_t start = json.find(key);
            if (start == std::string::npos) return "";
            start += key.length();
            size_t end = start;
            while (end < json.length()) {
                if (json[end] == '\"' && json[end-1] != '\\') break;
                end++;
            }
            std::string value = json.substr(start, end - start);
            size_t pos = 0;
            while ((pos = value.find("\\n", pos)) != std::string::npos) {
                value.replace(pos, 2, "\n");
                pos += 1;
            }
            pos = 0;
            while ((pos = value.find("\\\"", pos)) != std::string::npos) {
                value.replace(pos, 2, "\"");
                pos += 1;
            }
            return value;
        };
        
        if (response.find("{\"success\"") != std::string::npos) {
            std::string content = extractJsonField(response, "content");
            if (!content.empty()) return content;
        }
        
        if (response.find("\"status\":\"success\"") != std::string::npos && response.find("\"data\"") != std::string::npos) {
            std::string text = extractJsonField(response, "text");
            grok_last_images_.clear();
            size_t images_pos = response.find("\"images\":[");
            if (images_pos != std::string::npos) {
                size_t arr_start = images_pos + 10;
                size_t arr_end = response.find("]", arr_start);
                if (arr_end != std::string::npos) {
                    std::string arr = response.substr(arr_start, arr_end - arr_start);
                    size_t pos = 0;
                    while ((pos = arr.find("\"", pos)) != std::string::npos) {
                        size_t url_start = pos + 1;
                        size_t url_end = arr.find("\"", url_start);
                        if (url_end != std::string::npos) {
                            grok_last_images_.push_back(arr.substr(url_start, url_end - url_start));
                            pos = url_end + 1;
                        } else break;
                    }
                }
            }
            size_t conv_pos = response.find("\"conversation\":{");
            if (conv_pos != std::string::npos) {
                size_t id_pos = response.find("\"id\":\"", conv_pos);
                if (id_pos != std::string::npos) {
                    size_t id_start = id_pos + 6;
                    size_t id_end = response.find("\"", id_start);
                    if (id_end != std::string::npos) {
                        grok_last_conversation_id_ = response.substr(id_start, id_end - id_start);
                    }
                }
            }
            LOG_INFO("[AI] Grok parsed: text=" + std::to_string(text.length()) + " images=" + std::to_string(grok_last_images_.size()));
            if (!text.empty()) return text;
            if (!grok_last_images_.empty()) return "[AI生成了图片]";
            return "";
        }
        
        if (response.find("\"choices\"") != std::string::npos && response.find("\"message\"") != std::string::npos) {
            std::string content = extractJsonField(response, "content");
            if (!content.empty()) {
                LOG_INFO("[AI] OpenAI format parsed, content length: " + std::to_string(content.length()));
                return content;
            }
        }
        
        if (response.find("\"content\":[") != std::string::npos) {
            size_t text_pos = response.find("\"text\":\"");
            if (text_pos != std::string::npos) {
                std::string text = extractJsonField(response, "text");
                if (!text.empty()) return text;
                if (response.find("\"output_tokens\":0") != std::string::npos ||
                    response.find("\"text\":\"\"") != std::string::npos) {
                    last_error_ = ErrorCode::AI_API_EMPTY_RESPONSE;
                    LOG_INFO("[AI] API returned empty response (content filtered or model issue)");
                    return "";
                }
            }
        }
        
        if (response.find("\"answer\"") != std::string::npos) {
            std::string answer = extractJsonField(response, "answer");
            if (!answer.empty()) return answer;
        }
        
        if (response.find("\"response\"") != std::string::npos) {
            std::string resp = extractJsonField(response, "response");
            if (!resp.empty()) return resp;
        }
        
        if (response.find("\"text\"") != std::string::npos) {
            std::string text = extractJsonField(response, "text");
            if (!text.empty()) return text;
        }
        
        if (response.front() == '{') {
            last_error_ = ErrorCode::AI_API_UNKNOWN_FORMAT;
            LOG_ERROR(ErrorSystem::instance().formatError(last_error_, response.substr(0, 200)));
            return "";
        }
        
        return response;
#else
        return "";
#endif
    }
    
    GeneratedImage parseImageResponse(const std::string& response) {
        GeneratedImage result;
        
        if (response.find("\"content\":[") == std::string::npos) {
            result.text = parseApiResponse(response);
            return result;
        }
        
        size_t content_start = response.find("\"content\":[");
        if (content_start == std::string::npos) return result;
        
        size_t pos = content_start;
        while ((pos = response.find("\"type\":", pos)) != std::string::npos) {
            size_t type_start = response.find("\"", pos + 7);
            if (type_start == std::string::npos) break;
            size_t type_end = response.find("\"", type_start + 1);
            if (type_end == std::string::npos) break;
            
            std::string block_type = response.substr(type_start + 1, type_end - type_start - 1);
            
            if (block_type == "text") {
                size_t text_pos = response.find("\"text\":\"", pos);
                if (text_pos != std::string::npos && text_pos < pos + 200) {
                    size_t text_start = text_pos + 8;
                    size_t text_end = text_start;
                    while (text_end < response.length()) {
                        if (response[text_end] == '\"' && response[text_end-1] != '\\') break;
                        text_end++;
                    }
                    result.text += response.substr(text_start, text_end - text_start);
                }
            } else if (block_type == "image") {
                size_t source_pos = response.find("\"source\":", pos);
                if (source_pos != std::string::npos && source_pos < pos + 100) {
                    size_t media_pos = response.find("\"media_type\":\"", source_pos);
                    if (media_pos != std::string::npos) {
                        size_t media_start = media_pos + 14;
                        size_t media_end = response.find("\"", media_start);
                        if (media_end != std::string::npos) {
                            result.media_type = response.substr(media_start, media_end - media_start);
                        }
                    }
                    
                    size_t data_pos = response.find("\"data\":\"", source_pos);
                    if (data_pos != std::string::npos) {
                        size_t data_start = data_pos + 8;
                        size_t data_end = data_start;
                        while (data_end < response.length() && response[data_end] != '\"') {
                            data_end++;
                        }
                        result.base64 = response.substr(data_start, data_end - data_start);
                        LOG_INFO("[AI] Extracted generated image: " + std::to_string(result.base64.size()) + " chars base64");
                    }
                }
            }
            
            pos = type_end + 1;
        }
        
        size_t pos2 = 0;
        while ((pos2 = result.text.find("\\n", pos2)) != std::string::npos) {
            result.text.replace(pos2, 2, "\n");
            pos2 += 1;
        }
        
        return result;
    }
    
    using SetCardFunc = std::function<void(int64_t, int64_t, const std::string&)>;
    SetCardFunc set_card_func_;
    
    using SetTitleFunc = std::function<void(int64_t, int64_t, const std::string&)>;
    SetTitleFunc set_title_func_;
    
    std::string api_url_;
    std::string api_key_;
    std::string system_prompt_;
    std::string current_model_;
    std::map<std::string, ModelConfig> models_;
    ErrorCode last_error_ = ErrorCode::SUCCESS;
    std::string last_error_detail_;
    
    std::vector<std::string> grok_last_images_;
    std::string grok_last_conversation_id_;
    std::map<int64_t, std::string> grok_group_conversations_;
    
public:
    const std::vector<std::string>& getLastImages() const { return grok_last_images_; }
    
    std::string getGroupConversationId(int64_t group_id) {
        if (grok_group_conversations_.count(group_id)) {
            return grok_group_conversations_[group_id];
        }
        return "";
    }
    
    void setGroupConversationId(int64_t group_id, const std::string& conv_id) {
        grok_group_conversations_[group_id] = conv_id;
        LOG_INFO("[AI] Set group " + std::to_string(group_id) + " conversation: " + conv_id);
    }
    
    void updateGroupConversation(int64_t group_id) {
        if (!grok_last_conversation_id_.empty() && group_id > 0) {
            grok_group_conversations_[group_id] = grok_last_conversation_id_;
            saveGroupConversations();
        }
    }
    
    std::string callGrokNewConversation(const std::string& message) {
        std::string base_url = api_url_;
        size_t pos = base_url.rfind("/ask");
        if (pos != std::string::npos) {
            base_url = base_url.substr(0, pos);
        }
        std::string new_conv_url = base_url + "/conversation/new";
        
        std::string post_data = "{\"message\":\"" + escapeJson(message) + "\"}";
        std::string old_url = api_url_;
        api_url_ = new_conv_url;
        std::string response = callApiRaw(post_data, "application/json");
        api_url_ = old_url;
        
        LOG_INFO("[AI] Grok new conversation parsed length: " + std::to_string(response.length()));
        return response;
    }
    
    bool createNewConversation(int64_t group_id) {
        std::string base_url = api_url_;
        size_t pos = base_url.rfind("/ask");
        if (pos != std::string::npos) {
            base_url = base_url.substr(0, pos);
        }
        std::string new_conv_url = base_url + "/conversation/new";
        
        std::string old_url = api_url_;
        api_url_ = new_conv_url;
        std::string response = callApiRaw("{}", "application/json");
        api_url_ = old_url;
        
        if (response.find("\"status\":\"success\"") != std::string::npos) {
            grok_group_conversations_.erase(group_id);
            LOG_INFO("[AI] Created new conversation for group " + std::to_string(group_id));
            return true;
        }
        return false;
    }
    
    void saveGroupConversations() {
        std::ofstream file("config/grok_conversations.json");
        if (!file.is_open()) return;
        file << "{\n";
        bool first = true;
        for (const auto& [gid, cid] : grok_group_conversations_) {
            if (!first) file << ",\n";
            file << "  \"" << gid << "\": \"" << cid << "\"";
            first = false;
        }
        file << "\n}\n";
        file.close();
        LOG_INFO("[AI] Saved " + std::to_string(grok_group_conversations_.size()) + " group conversations");
    }
    
    void loadGroupConversations() {
        std::ifstream file("config/grok_conversations.json");
        if (!file.is_open()) return;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        size_t pos = 0;
        while ((pos = content.find("\"", pos)) != std::string::npos) {
            size_t gid_start = pos + 1;
            size_t gid_end = content.find("\"", gid_start);
            if (gid_end == std::string::npos) break;
            std::string gid_str = content.substr(gid_start, gid_end - gid_start);
            
            size_t cid_start = content.find("\"", gid_end + 1);
            if (cid_start == std::string::npos) break;
            cid_start++;
            size_t cid_end = content.find("\"", cid_start);
            if (cid_end == std::string::npos) break;
            std::string cid = content.substr(cid_start, cid_end - cid_start);
            
            try {
                int64_t gid = std::stoll(gid_str);
                if (!cid.empty()) {
                    grok_group_conversations_[gid] = cid;
                }
            } catch (...) {}
            pos = cid_end + 1;
        }
        LOG_INFO("[AI] Loaded " + std::to_string(grok_group_conversations_.size()) + " group conversations");
    }
};

}
