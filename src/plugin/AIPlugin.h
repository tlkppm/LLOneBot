#pragma once

#include "Plugin.h"
#include "../ai/AIService.h"
#include "../ai/PersonalitySystem.h"
#include "../core/Logger.h"
#include <regex>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace LCHBOT {

class AIPlugin : public IPlugin {
public:
    AIPlugin() {
        info_.name = "ai_chat";
        info_.version = "1.0.0";
        info_.author = "LCHBOT";
        info_.description = "AI智能聊天插件";
        info_.priority = 50;
    }
    
    PluginInfo getInfo() const override {
        return info_;
    }
    
    bool onLoad(PluginContext* context) override {
        context_ = context;
        LOG_INFO("[AI] Chat plugin loaded");
        return true;
    }
    
    void onUnload() override {
        LOG_INFO("[AI] Chat plugin unloaded");
    }
    
    void onEnable() override {}
    void onDisable() override {}
    
    bool onMessage(const MessageEvent& event) override {
        std::string raw = event.raw_message;
        
        std::string at_pattern = "\\[CQ:at,qq=" + std::to_string(event.self_id) + "[^\\]]*\\]";
        std::regex at_regex(at_pattern);
        
        if (!std::regex_search(raw, at_regex)) {
            return false;
        }
        
        std::string content = std::regex_replace(raw, at_regex, "");
        content = trim(content);
        
        if (content.empty()) {
            return false;
        }
        
        if (content[0] == '/') {
            return handleCommand(event, content);
        }
        
        return handleChat(event, content);
    }
    
    bool onPrivateMessage(const MessageEvent& event) override {
        std::string content = trim(event.raw_message);
        
        if (content.empty()) {
            return false;
        }
        
        if (content[0] == '/') {
            return handleCommand(event, content);
        }
        
        return handleChat(event, content);
    }
    
    bool onGroupMessage(const MessageEvent& event) override {
        return false;
    }
    
private:
    bool handleCommand(const MessageEvent& event, const std::string& content) {
        std::string cmd = content;
        std::string args;
        
        size_t space_pos = content.find(' ');
        if (space_pos != std::string::npos) {
            cmd = content.substr(0, space_pos);
            args = trim(content.substr(space_pos + 1));
        }
        
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        
        if (cmd == "/help") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            
            std::string help_text = 
                "=== " + current_name + " ===\n"
                "\xE6\x8C\x87\xE4\xBB\xA4\xE5\x88\x97\xE8\xA1\xA8\xEF\xBC\x9A\n"
                "  /help - \xE6\x98\xBE\xE7\xA4\xBA\xE5\xB8\xAE\xE5\x8A\xA9\n"
                "  /status - \xE6\x98\xBE\xE7\xA4\xBA\xE7\x8A\xB6\xE6\x80\x81\n"
                "  /clear - \xE6\xB8\x85\xE9\x99\xA4\xE4\xB8\x8A\xE4\xB8\x8B\xE6\x96\x87\n"
                "  /persona - \xE6\x9F\xA5\xE7\x9C\x8B\xE4\xBA\xBA\xE6\xA0\xBC\n"
                "  /persona <id> - \xE5\x88\x87\xE6\x8D\xA2\xE4\xBA\xBA\xE6\xA0\xBC\n"
                "  /about - \xE5\x85\xB3\xE4\xBA\x8E\n"
                "\n\xE8\x81\x8A\xE5\xA4\xA9\xEF\xBC\x9A@\xE6\x9C\xBA\xE5\x99\xA8\xE4\xBA\xBA \xE6\xB6\x88\xE6\x81\xAF";
            replyTo(event, help_text);
            return true;
        }
        
        if (cmd == "/status") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            
            std::string status_text = 
                "=== \xE7\x8A\xB6\xE6\x80\x81\xE4\xBF\xA1\xE6\x81\xAF ===\n"
                "\xE7\x8A\xB6\xE6\x80\x81\xEF\xBC\x9A\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD\n"
                "\xE7\x89\x88\xE6\x9C\xAC\xEF\xBC\x9A" "1.0.0\n"
                "\xE5\xBD\x93\xE5\x89\x8D\xE4\xBA\xBA\xE6\xA0\xBC\xEF\xBC\x9A" + current_name + "\n"
                "AI\xE5\xBC\x95\xE6\x93\x8E\xEF\xBC\x9AGemini-2.5\n"
                "\xE5\x8D\x8F\xE8\xAE\xAE\xEF\xBC\x9AOneBot 11";
            replyTo(event, status_text);
            return true;
        }
        
        if (cmd == "/clear") {
            if (event.isGroup()) {
                AIService::instance().clearContext(event.group_id, 0);
            } else {
                AIService::instance().clearContext(0, event.user_id);
            }
            replyTo(event, "\xE4\xB8\x8A\xE4\xB8\x8B\xE6\x96\x87\xE5\xB7\xB2\xE6\xB8\x85\xE9\x99\xA4 (^^)");
            return true;
        }
        
        if (cmd == "/persona") {
            auto& ps = PersonalitySystem::instance();
            
            if (args.empty()) {
                auto personalities = ps.listPersonalities();
                std::string list_text = "=== \xE5\x8F\xAF\xE7\x94\xA8\xE4\xBA\xBA\xE6\xA0\xBC ===\n";
                for (const auto& [id, name] : personalities) {
                    list_text += "  " + id + " - " + name + "\n";
                }
                list_text += "\n\xE4\xBD\xBF\xE7\x94\xA8 /persona <id> \xE5\x88\x87\xE6\x8D\xA2";
                replyTo(event, list_text);
            } else {
                bool success = false;
                if (event.isGroup()) {
                    success = ps.switchPersonalityForGroup(event.group_id, args);
                } else {
                    success = ps.switchPersonality(args);
                }
                
                if (success) {
                    std::string new_name = event.isGroup() ? 
                        ps.getNameForGroup(event.group_id) : ps.getCurrentName();
                    replyTo(event, "\xE4\xBA\xBA\xE6\xA0\xBC\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xE4\xB8\xBA\xEF\xBC\x9A" + new_name);
                    
                    if (event.isGroup()) {
                        AIService::instance().clearContext(event.group_id, 0);
                    } else {
                        AIService::instance().clearContext(0, event.user_id);
                    }
                } else {
                    replyTo(event, "\xE6\x9C\xAA\xE6\x89\xBE\xE5\x88\xB0\xE8\xAF\xA5\xE4\xBA\xBA\xE6\xA0\xBC\xEF\xBC\x8C\xE8\xAF\xB7\xE4\xBD\xBF\xE7\x94\xA8 /persona \xE6\x9F\xA5\xE7\x9C\x8B");
                }
            }
            return true;
        }
        
        if (cmd == "/about") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            
            std::string about_text = 
                "=== \xE5\x85\xB3\xE4\xBA\x8E " + current_name + " ===\n"
                "LCHBOT QQ\xE6\x9C\xBA\xE5\x99\xA8\xE4\xBA\xBA\xE6\xA1\x86\xE6\x9E\xB6\n"
                "OneBot 11\xE5\x8D\x8F\xE8\xAE\xAE\n"
                "AI\xE5\xBC\x95\xE6\x93\x8E\xEF\xBC\x9AGemini-2.5\n"
                "\xE4\xBC\x81\xE4\xB8\x9A\xE7\xBA\xA7\xE4\xBA\xBA\xE6\xA0\xBC\xE7\xB3\xBB\xE7\xBB\x9F";
            replyTo(event, about_text);
            return true;
        }
        
        return false;
    }
    
    bool handleChat(const MessageEvent& event, const std::string& content) {
        LOG_INFO("[AI] Chat: " + content.substr(0, 50) + "...");
        
        std::string sender_name = event.sender.card.empty() ? event.sender.nickname : event.sender.card;
        
        std::string response;
        if (event.isGroup()) {
            response = AIService::instance().chat(content, event.group_id, event.user_id, sender_name);
        } else {
            response = AIService::instance().chat(content, 0, event.user_id, sender_name);
        }
        
        if (response.empty()) {
            replyTo(event, "AI\xE6\x9C\x8D\xE5\x8A\xA1\xE6\x9A\x82\xE6\x97\xB6\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8 (>_<)");
            return true;
        }
        
        replyTo(event, response);
        return true;
    }
    
    void reply(const MessageEvent& event, const std::string& message) {
        if (!context_) return;
        
        if (event.isGroup()) {
            context_->getApi()->sendGroupMsg(event.group_id, message);
        } else {
            context_->getApi()->sendPrivateMsg(event.user_id, message);
        }
    }
    
    void replyTo(const MessageEvent& event, const std::string& message) {
        if (!context_) return;
        
        if (event.isGroup()) {
            context_->getApi()->sendGroupMsgReply(event.group_id, event.message_id, message);
        } else {
            context_->getApi()->sendPrivateMsgReply(event.user_id, event.message_id, message);
        }
    }
    
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }
    
    PluginInfo info_;
    PluginContext* context_ = nullptr;
};

}
