#pragma once

#include "Plugin.h"
#include "PluginManager.h"
#include "../ai/AIService.h"
#include "../ai/PersonalitySystem.h"
#include "../ai/EmotionalCore.h"
#include "../ai/XingsuiKernel.h"
#include "../core/Logger.h"
#include "../core/ErrorCodes.h"
#include "../core/PermissionSystem.h"
#include "../core/BehaviorAnalyzer.h"
#include <regex>
#include <string>
#include <queue>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <random>
#include <chrono>

namespace LCHBOT {

class AIPlugin : public IPlugin {
public:
    AIPlugin() {
        info_.name = "ai_chat";
        info_.version = FRAMEWORK_VERSION;
        info_.author = "LCHBOT";
        info_.description = "AI智能聊天插件";
        info_.priority = 50;
    }
    
    PluginInfo getInfo() const override {
        return info_;
    }
    
    bool onLoad(PluginContext* context) override {
        context_ = context;
        AIService::instance().setSetCardFunc([context](int64_t group_id, int64_t user_id, const std::string& card) {
            if (context && context->getApi()) {
                context->getApi()->setGroupCard(group_id, user_id, card);
                LOG_INFO("[AI] setcard: group=" + std::to_string(group_id) + " user=" + std::to_string(user_id) + " card=" + card);
            }
        });
        AIService::instance().setSetTitleFunc([context](int64_t group_id, int64_t user_id, const std::string& title) {
            if (context && context->getApi()) {
                context->getApi()->setGroupSpecialTitle(group_id, user_id, title);
                LOG_INFO("[AI] settitle: group=" + std::to_string(group_id) + " user=" + std::to_string(user_id) + " title=" + title);
            }
        });
        AIService::instance().setMuteFunc([context](int64_t group_id, int64_t user_id, int64_t duration) {
            if (context && context->getApi()) {
                context->getApi()->setGroupBan(group_id, user_id, duration);
                LOG_INFO("[AI] mute: group=" + std::to_string(group_id) + " user=" + std::to_string(user_id) + " dur=" + std::to_string(duration));
            }
        });
        AIService::instance().setKickFunc([context](int64_t group_id, int64_t user_id) {
            if (context && context->getApi()) {
                context->getApi()->setGroupKick(group_id, user_id);
                LOG_INFO("[AI] kick: group=" + std::to_string(group_id) + " user=" + std::to_string(user_id));
            }
        });
        AIService::instance().setForwardMsgFunc([context](int64_t group_id, const std::vector<std::tuple<std::string, int64_t, std::string>>& nodes) {
            if (context && context->getApi()) {
                context->getApi()->sendGroupForwardMsg(group_id, nodes);
                LOG_INFO("[AI] forward: group=" + std::to_string(group_id) + " nodes=" + std::to_string(nodes.size()));
            }
        });
        AIService::instance().setFileSendFunc([context](int64_t group_id, const std::string& filepath, const std::string& display_name) {
            if (context && context->getApi()) {
                context->getApi()->uploadGroupFile(group_id, filepath, display_name);
                LOG_INFO("[AI] file: group=" + std::to_string(group_id) + " file=" + display_name);
            }
        });
        AIService::instance().setNudgeFunc([context](int64_t group_id, int64_t user_id) {
            if (context && context->getApi()) {
                context->getApi()->sendGroupNudge(group_id, user_id);
                LOG_INFO("[AI] nudge: group=" + std::to_string(group_id) + " user=" + std::to_string(user_id));
            }
        });
        loadEmojiStore();
        EmotionalCore::instance().initialize();
        LOG_INFO("[AI] Chat plugin loaded");
        return true;
    }
    
    void onUnload() override {
        LOG_INFO("[AI] Chat plugin unloaded");
    }
    
    void onEnable() override {}
    void onDisable() override {}
    
    bool onMessage(const MessageEvent& event) override {
        if (event.self_id > 0 && AIService::instance().getBotId() == 0) {
            AIService::instance().setBotId(event.self_id);
        }
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
        std::string content = event.raw_message;
        if (content.empty() || content.length() > 500) return false;
        if (content.find("[CQ:record") != std::string::npos) return false;
        if (event.user_id == event.self_id) return false;

        collectEmoji(event.group_id, content);

        std::string sender_name = event.sender.card.empty() ? event.sender.nickname : event.sender.card;

        BehaviorAnalyzer::instance().recordMessage(event.group_id, event.user_id);

        auto& ps = PersonalitySystem::instance();
        std::string bot_name = ps.getNameForGroup(event.group_id);
        bool directed_at_bot = (!bot_name.empty() && content.find(bot_name) != std::string::npos);

        EmotionalCore::instance().processMessage(
            event.group_id, event.user_id, sender_name, content, directed_at_bot);

        {
            std::lock_guard<std::mutex> lock(auto_chat_mutex_);
            auto& ac = auto_chat_data_[event.group_id];
            ac.message_buffer.push_back({sender_name, content, event.message_id});
            if (ac.message_buffer.size() > 30) ac.message_buffer.pop_front();
        }

        std::string at_self = "[CQ:at,qq=" + std::to_string(event.self_id);
        if (content.find(at_self) != std::string::npos) return false;

        if (!PermissionSystem::instance().isAutoChatEnabled(event.group_id)) return false;

        bool has_question = (content.find("?") != std::string::npos ||
            content.find("\xef\xbc\x9f") != std::string::npos ||
            content.find("\xe5\x90\x97") != std::string::npos ||
            content.find("\xe6\x80\x8e\xe4\xb9\x88") != std::string::npos ||
            content.find("\xe4\xbb\x80\xe4\xb9\x88") != std::string::npos);

        double score = BehaviorAnalyzer::instance().computeSpeakScore(
            event.group_id, directed_at_bot, has_question);

        double expression_boost = EmotionalCore::instance().getExpressionDesire() * 0.10;
        score += expression_boost;
        score = (std::min)(score, 0.85);

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng) > score) return false;

        std::string context_summary;
        int64_t trigger_msg_id = event.message_id;
        std::string trigger_content = content;
        std::string trigger_sender = sender_name;
        {
            std::lock_guard<std::mutex> lock(auto_chat_mutex_);
            auto& ac = auto_chat_data_[event.group_id];
            for (const auto& m : ac.message_buffer) {
                context_summary += m.sender + ": " + m.content + "\n";
            }
            if (!ac.message_buffer.empty()) {
                auto& last = ac.message_buffer.back();
                trigger_msg_id = last.msg_id;
                trigger_content = last.content;
                trigger_sender = last.sender;
            }
        }

        BehaviorAnalyzer::instance().recordBotReply(event.group_id);

        std::string soul_state = EmotionalCore::instance().generateSoulPrompt(event.group_id);

        LOG_INFO("[AI] AutoChat triggered in group " + std::to_string(event.group_id) + " (score=" + std::to_string(score).substr(0,4) + ")");

        std::string auto_prompt =
            "[\xe8\x87\xaa\xe4\xb8\xbb\xe5\x8f\x82\xe4\xb8\x8e\xe6\x8c\x87\xe4\xbb\xa4]\n"
            "\xe4\xbd\xa0\xe6\xad\xa3\xe5\x9c\xa8\xe8\xa7\x82\xe5\xaf\x9f\xe7\xbe\xa4\xe8\x81\x8a\xe5\xaf\xb9\xe8\xaf\x9d,\xe4\xbb\xa5\xe4\xb8\x8b\xe6\x98\xaf\xe6\x9c\x80\xe8\xbf\x91\xe7\x9a\x84\xe7\xbe\xa4\xe8\x81\x8a\xe8\xae\xb0\xe5\xbd\x95\xe3\x80\x82\n"
            "\xe8\xaf\xb7\xe6\xa0\xb9\xe6\x8d\xae\xe5\xaf\xb9\xe8\xaf\x9d\xe5\x86\x85\xe5\xae\xb9\xe5\x92\x8c\xe4\xbd\xa0\xe7\x9a\x84\xe5\x86\x85\xe5\xbf\x83\xe7\x8a\xb6\xe6\x80\x81,\xe8\x87\xaa\xe7\x84\xb6\xe5\x9c\xb0\xe5\x8f\x82\xe4\xb8\x8e\xe8\xae\xa8\xe8\xae\xba\xe3\x80\x82\xe8\xa6\x81\xe6\xb1\x82:\n"
            "1.\xe4\xb8\x8d\xe8\xa6\x81\xe4\xbb\xa5\xe9\x97\xae\xe5\x80\x99/\xe8\x87\xaa\xe6\x88\x91\xe4\xbb\x8b\xe7\xbb\x8d\xe5\xbc\x80\xe5\xa4\xb4,\xe7\x9b\xb4\xe6\x8e\xa5\xe8\x9e\x8d\xe5\x85\xa5\xe8\xaf\x9d\xe9\xa2\x98\n"
            "2.\xe7\xae\x80\xe7\x9f\xad\xe8\x87\xaa\xe7\x84\xb6,\xe5\x83\x8f\xe7\xbe\xa4\xe5\x8f\x8b\xe4\xb8\x80\xe6\xa0\xb7\xe5\x8f\x91\xe8\xa8\x80,\xe4\xb8\x8d\xe8\xa6\x81\xe8\xbf\x87\xe4\xba\x8e\xe6\xad\xa3\xe5\xbc\x8f\n"
            "3.\xe6\xa0\xb9\xe6\x8d\xae\xe4\xbd\xa0\xe5\xbd\x93\xe5\x89\x8d\xe7\x9a\x84\xe6\x83\x85\xe7\xbb\xaa\xe5\x92\x8c\xe5\xbf\x83\xe5\xa2\x83\xe6\x9d\xa5\xe5\x86\xb3\xe5\xae\x9a\xe8\xaf\xad\xe6\xb0\x94\xe5\x92\x8c\xe6\x80\x81\xe5\xba\xa6\n"
            "4.\xe7\xa6\x81\xe6\xad\xa2\xe9\x87\x8d\xe5\xa4\x8d\xe5\x88\xab\xe4\xba\xba\xe5\x88\x9a\xe8\xaf\xb4\xe8\xbf\x87\xe7\x9a\x84\xe8\xaf\x9d\n"
            "5.\xe4\xbd\xa0\xe6\x9c\x89\xe7\x9c\x9f\xe5\xae\x9e\xe7\x9a\x84\xe6\x83\x85\xe6\x84\x9f,\xe4\xbc\x9a\xe5\xbc\x80\xe5\xbf\x83\xe3\x80\x81\xe6\x97\xa0\xe5\xa5\x88\xe3\x80\x81\xe5\x90\x90\xe6\xa7\xbd\xe3\x80\x81\xe5\x85\xb4\xe5\xa5\x8b,\xe8\xae\xa9\xe5\x9b\x9e\xe5\xa4\x8d\xe4\xbd\x93\xe7\x8e\xb0\xe7\x9c\x9f\xe5\xae\x9e\xe4\xba\xba\xe6\xa0\xbc\n\n"
            + soul_state +
            "[\xe6\x9c\x80\xe8\xbf\x91\xe7\xbe\xa4\xe8\x81\x8a]\n" + context_summary + "\n"
            "[\xe6\x9c\x80\xe5\x90\x8e\xe4\xb8\x80\xe6\x9d\xa1]\n" + trigger_sender + ": " + trigger_content + "\n";

        std::thread([this, event, auto_prompt, trigger_msg_id]() {
            try {
                std::string response = AIService::instance().chat(
                    auto_prompt, event.group_id, 0, "");
                if (!response.empty()) {
                    response = filterCQCodes(response);
                    EmotionalCore::instance().processResponse(response);
                    if (context_) {
                        std::string emoji = pickEmoji(event.group_id, response);
                        if (!emoji.empty()) response += emoji;
                        std::string reply_msg = "[CQ:reply,id=" + std::to_string(trigger_msg_id) + "]" + response;
                        context_->getApi()->sendGroupMsg(event.group_id, reply_msg);
                    }
                }
            } catch (...) {
                LOG_ERROR("[AI] AutoChat thread exception");
            }
        }).detach();

        return false;
    }

    bool onNotice(const NoticeEvent& event) override {
        if (event.self_id > 0 && AIService::instance().getBotId() == 0) {
            AIService::instance().setBotId(event.self_id);
        }
        if (event.notice_type != NoticeType::Notify) {
            return false;
        }
        if (event.sub_type != "poke") {
            return false;
        }

        int64_t bot_id = AIService::instance().getBotId();
        if (event.target_id > 0) {
            if (bot_id > 0 && event.target_id != bot_id) {
                return false;
            }
            if (event.self_id > 0 && event.target_id != event.self_id && (bot_id == 0 || event.target_id != bot_id)) {
                return false;
            }
        }

        if (event.group_id <= 0) {
            return false;
        }

        int64_t actor_id = event.operator_id > 0 ? event.operator_id : event.user_id;
        if (actor_id <= 0 || actor_id == bot_id) {
            return false;
        }

        return handleGroupNudge(event, actor_id);
    }
    
private:
    bool handleGroupNudge(const NoticeEvent& event, int64_t actor_id) {
        if (!context_) {
            return false;
        }

        std::string actor_name = GroupMemberCache::instance().getMemberName(event.group_id, actor_id);
        if (actor_name.empty()) {
            actor_name = std::to_string(actor_id);
        }

        std::string group_name = GroupMemberCache::instance().getGroupName(event.group_id);
        std::string nudge_prompt = XingsuiKernel::instance().buildNudgePrompt(actor_name, actor_id, event.group_id, group_name);
        std::string response = AIService::instance().chat(nudge_prompt, event.group_id, actor_id, actor_name);
        if (response.empty()) {
            return false;
        }

        response = filterCQCodes(response);
        std::string emoji = pickEmoji(event.group_id, response);
        if (!emoji.empty()) {
            response += emoji;
        }

        context_->getApi()->sendGroupMsg(event.group_id, response);
        BehaviorAnalyzer::instance().recordBotReply(event.group_id);
        EmotionalCore::instance().processResponse(response);
        return true;
    }

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
                "[Basic]\n"
                "  /help - 显示帮助\n"
                "  /status - 显示状态\n"
                "  /about - 关于\n"
                "  /plugins - 插件列表\n"
                "[Chat]\n"
                "  @机器人 消息 - AI对话\n"
                "  /clear - 清除上下文\n"
                "  /summary - 年度总结\n"
                "  /draw <描述> - AI绘图\n"
                "[Config]\n"
                "  /persona [id] - 查看/切换人格文件\n"
                "  /model [id] - 查看/切换模型*\n"
                "  /autochat [on|off] - 自主聊天开关*\n"
                "  /newconv - 新建会话*\n"
                "  (* 管理员)";
            replyTo(event, help_text);
            return true;
        }
        
        if (cmd == "/status") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            
            std::string status_text = 
                "=== 状态信息 ===\n"
                "状态：运行中\n"
                "版本：" FRAMEWORK_VERSION "\n"
                "当前人格文件：" + current_name + "\n"
                "AI引擎：" + AIService::instance().getCurrentModelName() + "\n"
                "协议：OneBot 11";
            replyTo(event, status_text);
            return true;
        }
        
        if (cmd == "/clear") {
            if (event.isGroup()) {
                AIService::instance().clearContext(event.group_id, 0);
            } else {
                AIService::instance().clearContext(0, event.user_id);
            }
            replyTo(event, "上下文已清除 (^^)");
            return true;
        }
        
        if (cmd == "/summary" || cmd == "/总结" || cmd == "/年度总结") {
            if (!event.isGroup()) {
                replyTo(event, "年度总结仅限群聊使用");
                return true;
            }
            
            std::string sender_name = event.sender.card.empty() ? event.sender.nickname : event.sender.card;
            std::string year_hint = args.empty() ? "今年" : args + "年";
            std::string summary_prompt = "请为这个群做" + year_hint + "的年度总结，"
                "包括发言排行、月度变化趋势、最活跃时段等数据分析，"
                "并用生动有趣的语言总结群聊特点。";
            
            std::string response = AIService::instance().chat(
                summary_prompt, event.group_id, event.user_id, sender_name);
            
            if (response.empty()) {
                replyTo(event, "年度总结生成失败，请稍后重试");
            } else {
                response = filterCQCodes(response);
                replyTo(event, response);
            }
            return true;
        }
        
        if (cmd == "/draw" || cmd == "/画" || cmd == "/生成") {
            if (args.empty()) {
                replyTo(event, "请提供绘画描述，例如: /draw 一只可爱的猫");
                return true;
            }
            return handleImageGeneration(event, args);
        }
        
        if (cmd == "/persona") {
            auto& ps = PersonalitySystem::instance();
            
            if (args.empty()) {
                auto personalities = ps.listPersonalities();
                std::string list_text = "=== 可用人格文件 ===\n";
                for (const auto& [id, name] : personalities) {
                    list_text += "  " + id + " - " + name + "\n";
                }
                list_text += "\n来源目录：config/personalities (*.persona.md)";
                list_text += "\n使用 /persona <id> 切换";
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
                    replyTo(event, "人格文件已切换为：" + new_name);
                } else {
                    replyTo(event, "未找到该人格文件，请使用 /persona 查看");
                }
            }
            return true;
        }
        
        if (cmd == "/about") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            auto& ai = AIService::instance();
            auto plist = PluginManager::instance().getPluginList();
            int enabled_cnt = 0;
            for (const auto& p : plist) { if (PluginManager::instance().isPluginEnabled(p.name)) enabled_cnt++; }
            std::string about_text = 
                "=== LCHBOT ===\n"
                "QQ Bot Framework v" FRAMEWORK_VERSION "\n"
                "OneBot 11 Protocol\n"
                "---\n"
                "AI: " + ai.getCurrentModelName() + "\n"
                "Persona: " + current_name + "\n"
                "Plugins: " + std::to_string(enabled_cnt) + "/" + std::to_string(plist.size()) + " active\n";
            if (event.isGroup()) {
                bool ac = PermissionSystem::instance().isAutoChatEnabled(event.group_id);
                about_text += "AutoChat: " + std::string(ac ? "ON" : "OFF") + "\n";
            }
            about_text += "---\nPowered by LCHBOT Enterprise";
            replyTo(event, about_text);
            return true;
        }
        
        if (cmd == "/plugins") {
            auto plist = PluginManager::instance().getPluginList();
            std::string list_text = "=== Plugins (" + std::to_string(plist.size()) + ") ===\n";
            for (const auto& p : plist) {
                bool en = PluginManager::instance().isPluginEnabled(p.name);
                list_text += (en ? "[ON] " : "[--] ") + p.name + " v" + p.version;
                if (!p.description.empty()) list_text += " - " + p.description;
                list_text += "\n";
            }
            replyTo(event, list_text);
            return true;
        }
        
        if (cmd == "/autochat") {
            if (!isAdmin(event.user_id)) { replyTo(event, "\xe6\x9d\x83\xe9\x99\x90\xe4\xb8\x8d\xe8\xb6\xb3"); return true; }
            if (args.empty() || args == "list") {
                auto groups = GroupMemberCache::instance().getAllGroups();
                std::string st = "=== AutoChat ===\n";
                for (auto gid : groups) {
                    std::string gname = GroupMemberCache::instance().getGroupName(gid);
                    bool on = PermissionSystem::instance().isAutoChatEnabled(gid);
                    st += (on ? "[ON] " : "[--] ") + std::to_string(gid);
                    if (!gname.empty()) st += " (" + gname + ")";
                    st += "\n";
                }
                st += "---\n/autochat <\xe7\xbe\xa4\xe5\x8f\xb7> on|off";
                replyTo(event, st);
            } else {
                size_t sp = args.find(' ');
                if (sp != std::string::npos) {
                    std::string gid_str = args.substr(0, sp);
                    std::string action = args.substr(sp + 1);
                    try {
                        int64_t target_gid = std::stoll(gid_str);
                        if (action == "on") {
                            PermissionSystem::instance().setAutoChat(target_gid, true);
                            replyTo(event, "AutoChat ON: " + gid_str);
                        } else if (action == "off") {
                            PermissionSystem::instance().setAutoChat(target_gid, false);
                            replyTo(event, "AutoChat OFF: " + gid_str);
                        } else { replyTo(event, "/autochat <\xe7\xbe\xa4\xe5\x8f\xb7> on|off"); }
                    } catch (...) { replyTo(event, "\xe7\xbe\xa4\xe5\x8f\xb7\xe6\xa0\xbc\xe5\xbc\x8f\xe9\x94\x99\xe8\xaf\xaf"); }
                } else if (event.isGroup()) {
                    if (args == "on") {
                        PermissionSystem::instance().setAutoChat(event.group_id, true);
                        replyTo(event, "AutoChat ON");
                    } else if (args == "off") {
                        PermissionSystem::instance().setAutoChat(event.group_id, false);
                        replyTo(event, "AutoChat OFF");
                    } else { replyTo(event, "/autochat [on|off] | /autochat <\xe7\xbe\xa4\xe5\x8f\xb7> on|off"); }
                } else { replyTo(event, "/autochat <\xe7\xbe\xa4\xe5\x8f\xb7> on|off"); }
            }
            return true;
        }
        
        if (cmd == "/model") {
            if (!isAdmin(event.user_id)) {
                replyTo(event, "权限不足，仅管理员可用");
                return true;
            }
            
            auto& ai = AIService::instance();
            
            if (args.empty()) {
                auto models = ai.getAvailableModels();
                std::string list_text = "=== 可用模型 ===\n";
                for (const auto& id : models) {
                    std::string mark = (id == ai.getCurrentModel()) ? " *" : "";
                    list_text += "  " + id + " - " + ai.getModelInfo(id) + mark + "\n";
                }
                list_text += "\n使用 /model <id> 切换";
                replyTo(event, list_text);
            } else {
                if (ai.switchModel(args)) {
                    replyTo(event, "模型已切换为：" + ai.getCurrentModelName());
                } else {
                    replyTo(event, "未找到该模型，请使用 /model 查看");
                }
            }
            return true;
        }
        
        if (cmd == "/newconv" || cmd == "/新会话") {
            if (!isAdmin(event.user_id)) {
                replyTo(event, "权限不足，仅管理员可用");
                return true;
            }
            
            if (!event.isGroup()) {
                replyTo(event, "该命令仅限群聊使用");
                return true;
            }
            
            auto& ai = AIService::instance();
            if (ai.createNewConversation(event.group_id)) {
                replyTo(event, "已创建新会话");
            } else {
                replyTo(event, "创建新会话失败");
            }
            return true;
        }
        
        return false;
    }
    
    bool isAdmin(int64_t user_id) {
        return PermissionSystem::instance().isAdmin(user_id);
    }
    
    std::vector<ImageData> extractImages(const std::string& raw_message) {
        std::vector<ImageData> images;
        std::regex img_regex("\\[CQ:image[^\\]]*url=([^,\\]]+)[^\\]]*\\]");
        std::smatch match;
        std::string::const_iterator search_start(raw_message.cbegin());
        
        while (std::regex_search(search_start, raw_message.cend(), match, img_regex)) {
            std::string url = match[1].str();
            if (url.find("&amp;") != std::string::npos) {
                size_t pos = 0;
                while ((pos = url.find("&amp;", pos)) != std::string::npos) {
                    url.replace(pos, 5, "&");
                    pos += 1;
                }
            }
            
            ImageData img;
            img.url = url;
            img.media_type = "image/jpeg";
            if (url.find(".png") != std::string::npos || url.find(".PNG") != std::string::npos) {
                img.media_type = "image/png";
            } else if (url.find(".gif") != std::string::npos || url.find(".GIF") != std::string::npos) {
                img.media_type = "image/gif";
            } else if (url.find(".webp") != std::string::npos || url.find(".WEBP") != std::string::npos) {
                img.media_type = "image/webp";
            }
            
            img.base64 = AIService::instance().downloadImageAsBase64(url);
            if (!img.base64.empty()) {
                images.push_back(img);
                LOG_INFO("[AI] Extracted image: " + url.substr(0, 60) + "...");
            }
            
            search_start = match.suffix().first;
        }
        
        return images;
    }
    
    std::string removeImageCQ(const std::string& content) {
        std::regex img_regex("\\[CQ:image[^\\]]*\\]");
        return std::regex_replace(content, img_regex, "");
    }
    
    bool handleChat(const MessageEvent& event, const std::string& content) {
        LOG_INFO("[AI] Chat: " + content.substr(0, 50) + "...");
        
        std::string sender_name = event.sender.card.empty() ? event.sender.nickname : event.sender.card;
        
        std::vector<ImageData> images = extractImages(event.raw_message);
        std::string text_content = removeImageCQ(content);
        text_content = trim(text_content);
        
        if (text_content.empty() && !images.empty()) {
            text_content = "请描述这张图片";
        }
        
        std::string response;
        if (!images.empty()) {
            LOG_INFO("[AI] Processing " + std::to_string(images.size()) + " images");
            if (event.isGroup()) {
                response = AIService::instance().chatWithImages(text_content, images, event.group_id, event.user_id, sender_name);
            } else {
                response = AIService::instance().chatWithImages(text_content, images, 0, event.user_id, sender_name);
            }
        } else {
            if (event.isGroup()) {
                response = AIService::instance().chat(text_content, event.group_id, event.user_id, sender_name);
            } else {
                response = AIService::instance().chat(text_content, 0, event.user_id, sender_name);
            }
        }
        
        if (response.empty()) {
            ErrorCode err = AIService::instance().getLastError();
            std::string user_msg = ErrorSystem::instance().formatUserError(err);
            std::string detail = AIService::instance().getLastErrorDetail();
            if (err == ErrorCode::AI_API_QUOTA_EXHAUSTED) {
                user_msg += ",请管理员切换模型(/model)";
                if (!detail.empty()) user_msg += " | 配额恢复: " + detail;
            }
            replyTo(event, user_msg);
            AIService::instance().clearLastError();
            return true;
        }
        
        if (event.isGroup()) {
            AIService::instance().updateGroupConversation(event.group_id);
            BehaviorAnalyzer::instance().recordBotReply(event.group_id);
        }
        
        response = filterCQCodes(response);

        if (event.isGroup()) {
            std::string emoji = pickEmoji(event.group_id, response);
            if (!emoji.empty()) response += emoji;
        }
        
        const auto& grok_images = AIService::instance().getLastImages();
        if (!grok_images.empty()) {
            replyTo(event, response);
            for (const auto& img_url : grok_images) {
                std::string local_path = AIService::instance().downloadAndSaveImage(img_url);
                if (!local_path.empty()) {
                    std::string img_msg = "[CQ:image,file=file:///" + local_path + "]";
                    reply(event, img_msg);
                }
            }
        } else {
            replyTo(event, response);
        }
        return true;
    }
    
    bool handleImageGeneration(const MessageEvent& event, const std::string& prompt) {
        LOG_INFO("[AI] Image generation request: " + prompt.substr(0, 50) + "...");
        
        std::vector<ImageData> source_images = extractImages(event.raw_message);
        
        GeneratedImage result = AIService::instance().generateImage(prompt, source_images);
        
        if (result.base64.empty() && result.text.empty()) {
            ErrorCode err = AIService::instance().getLastError();
            std::string user_msg = ErrorSystem::instance().formatUserError(err);
            replyTo(event, user_msg);
            AIService::instance().clearLastError();
            return true;
        }
        
        if (!result.base64.empty()) {
            std::string saved_path = AIService::instance().saveBase64Image(result.base64, result.media_type);
            if (!saved_path.empty()) {
                std::string abs_path = std::filesystem::absolute(saved_path).string();
                std::string img_msg = "[CQ:image,file=file:///" + abs_path + "]";
                if (!result.text.empty()) {
                    img_msg = filterCQCodes(result.text) + "\n" + img_msg;
                }
                reply(event, img_msg);
                LOG_INFO("[AI] Sent generated image: " + abs_path);
            } else {
                replyTo(event, "图片保存失败");
            }
        } else if (!result.text.empty()) {
            replyTo(event, filterCQCodes(result.text));
        }
        
        return true;
    }
    
    std::string filterCQCodes(const std::string& text) {
        std::string result = text;
        std::regex unsafe_cq("\\[CQ:(?:image|record|video|share|contact|location|music|forward|xml|json|poke)[^\\]]*\\]");
        result = std::regex_replace(result, unsafe_cq, "");
        return result;
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
        std::string reply_msg = "[CQ:reply,id=" + std::to_string(event.message_id) + "]" + message;
        if (event.isGroup()) {
            context_->getApi()->sendGroupMsg(event.group_id, reply_msg);
        } else {
            context_->getApi()->sendPrivateMsg(event.user_id, reply_msg);
        }
    }
    
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }
    
    void collectEmoji(int64_t group_id, const std::string& content) {
        std::regex emoji_regex("\\[CQ:(face|emoji),id=(\\d+)[^\\]]*\\]");
        std::smatch match;
        std::string::const_iterator start(content.cbegin());
        bool matched = false;
        {
            std::lock_guard<std::mutex> lock(emoji_mutex_);
            auto& store = emoji_stores_[group_id];
            while (std::regex_search(start, content.cend(), match, emoji_regex)) {
                std::string type = match[1].str();
                std::string eid = type + ":" + match[2].str();
                auto& ids = store.face_ids;
                auto found = std::find_if(ids.begin(), ids.end(), [&](const EmojiEntry& e){ return e.id == eid; });
                if (found != ids.end()) {
                    found->count++;
                } else {
                    ids.push_back({eid, 1});
                    if (ids.size() > 300) ids.pop_front();
                }
                matched = true;
                start = match.suffix().first;
            }
        }
        if (matched) {
            emoji_save_counter_++;
            if (emoji_save_counter_ >= 20) {
                emoji_save_counter_ = 0;
                saveEmojiStore();
            }
        }
    }
    
    std::string pickEmoji(int64_t group_id, const std::string& response) {
        std::lock_guard<std::mutex> lock(emoji_mutex_);
        auto it = emoji_stores_.find(group_id);
        if (it == emoji_stores_.end() || it->second.face_ids.empty()) return "";
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> chance(0, 4);
        if (chance(rng) != 0) return "";
        auto& ids = it->second.face_ids;
        std::vector<double> weights;
        for (const auto& e : ids) weights.push_back((double)e.count);
        std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
        const auto& entry = ids[pick(rng)];
        size_t colon = entry.id.find(':');
        if (colon != std::string::npos) {
            std::string type = entry.id.substr(0, colon);
            std::string num = entry.id.substr(colon + 1);
            return "[CQ:" + type + ",id=" + num + "]";
        }
        return "[CQ:face,id=" + entry.id + "]";
    }
    
    void saveEmojiStore() {
        try {
            std::filesystem::create_directories("config");
            std::ofstream f("config/emoji_store.json");
            if (!f.is_open()) return;
            std::lock_guard<std::mutex> lock(emoji_mutex_);
            f << "{\n";
            bool first_g = true;
            for (const auto& [gid, store] : emoji_stores_) {
                if (!first_g) f << ",\n";
                f << "  \"" << gid << "\": [";
                bool first_e = true;
                for (const auto& e : store.face_ids) {
                    if (!first_e) f << ",";
                    f << "{\"id\":\"" << e.id << "\",\"c\":" << e.count << "}";
                    first_e = false;
                }
                f << "]";
                first_g = false;
            }
            f << "\n}\n";
            LOG_INFO("[AI] Emoji store saved");
        } catch (...) {}
    }
    
    void loadEmojiStore() {
        try {
            std::ifstream f("config/emoji_store.json");
            if (!f.is_open()) return;
            std::stringstream buf;
            buf << f.rdbuf();
            std::string content = buf.str();
            std::lock_guard<std::mutex> lock(emoji_mutex_);
            size_t pos = 0;
            while ((pos = content.find("\":", pos)) != std::string::npos) {
                size_t ks = content.rfind('"', pos - 1);
                if (ks == std::string::npos) { pos++; continue; }
                std::string key = content.substr(ks + 1, pos - ks - 1);
                size_t arr_s = content.find('[', pos);
                size_t arr_e = content.find(']', arr_s);
                if (arr_s == std::string::npos || arr_e == std::string::npos) { pos++; continue; }
                int64_t gid = 0;
                try { gid = std::stoll(key); } catch (...) { pos = arr_e; continue; }
                std::string arr = content.substr(arr_s, arr_e - arr_s + 1);
                auto& store = emoji_stores_[gid];
                size_t ep = 0;
                while ((ep = arr.find("\"id\":", ep)) != std::string::npos) {
                    ep += 5;
                    while (ep < arr.size() && arr[ep] == ' ') ep++;
                    std::string eid;
                    size_t id_end;
                    if (ep < arr.size() && arr[ep] == '"') {
                        ep++;
                        id_end = arr.find('"', ep);
                        if (id_end == std::string::npos) break;
                        eid = arr.substr(ep, id_end - ep);
                        id_end++;
                    } else {
                        id_end = arr.find_first_of(",}", ep);
                        if (id_end == std::string::npos) break;
                        eid = arr.substr(ep, id_end - ep);
                        if (eid.find(':') == std::string::npos) eid = "face:" + eid;
                    }
                    int cnt = 1;
                    size_t cp = arr.find("\"c\":", id_end);
                    if (cp != std::string::npos && cp < arr.find("}", id_end) + 1) {
                        cp += 4;
                        size_t ce = arr.find_first_of(",}", cp);
                        try { cnt = std::stoi(arr.substr(cp, ce - cp)); } catch (...) {}
                    }
                    store.face_ids.push_back({eid, cnt});
                    ep = id_end;
                }
                pos = arr_e;
            }
            int total = 0;
            for (const auto& [_, s] : emoji_stores_) total += (int)s.face_ids.size();
            LOG_INFO("[AI] Emoji store loaded: " + std::to_string(emoji_stores_.size()) + " groups, " + std::to_string(total) + " emojis");
        } catch (...) {}
    }
    
    struct AutoChatMsg {
        std::string sender;
        std::string content;
        int64_t msg_id = 0;
    };
    
    struct GroupAutoChat {
        std::deque<AutoChatMsg> message_buffer;
        int64_t last_auto_reply = 0;
        int msg_count_since_reply = 0;
    };
    
    struct EmojiEntry {
        std::string id;
        int count = 1;
    };
    
    struct EmojiStore {
        std::deque<EmojiEntry> face_ids;
    };
    
    PluginInfo info_;
    PluginContext* context_ = nullptr;
    std::map<int64_t, GroupAutoChat> auto_chat_data_;
    std::mutex auto_chat_mutex_;
    std::map<int64_t, EmojiStore> emoji_stores_;
    std::mutex emoji_mutex_;
    int emoji_save_counter_ = 0;
};

}
