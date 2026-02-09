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
#include <filesystem>

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
        std::string content = event.raw_message;
        if (content.empty() || content.length() > 500) {
            return false;
        }
        
        if (content.find("[CQ:image") != std::string::npos ||
            content.find("[CQ:face") != std::string::npos ||
            content.find("[CQ:record") != std::string::npos) {
            return false;
        }
        
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
                "指令列表：\n"
                "  /help - 显示帮助\n"
                "  /status - 显示状态\n"
                "  /clear - 清除上下文\n"
                "  /summary - 年度总结\n"
                "  /persona - 查看人格\n"
                "  /persona <id> - 切换人格\n"
                "  /model - 查看模型(管理员)\n"
                "  /model <id> - 切换模型(管理员)\n"
                "  /about - 关于\n"
                "\n聊天：@机器人 消息";
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
                "版本：1.0.0\n"
                "当前人格：" + current_name + "\n"
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
                std::string list_text = "=== 可用人格 ===\n";
                for (const auto& [id, name] : personalities) {
                    list_text += "  " + id + " - " + name + "\n";
                }
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
                    replyTo(event, "人格已切换为：" + new_name);
                } else {
                    replyTo(event, "未找到该人格，请使用 /persona 查看");
                }
            }
            return true;
        }
        
        if (cmd == "/about") {
            auto& ps = PersonalitySystem::instance();
            std::string current_name = event.isGroup() ? 
                ps.getNameForGroup(event.group_id) : ps.getCurrentName();
            
            std::string about_text = 
                "=== 关于 " + current_name + " ===\n"
                "LCHBOT QQ机器人框架\n"
                "OneBot 11协议\n"
                "AI引擎：" + AIService::instance().getCurrentModelName() + "\n"
                "企业级人格系统";
            replyTo(event, about_text);
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
        static std::vector<int64_t> admins = {2643518036};
        return std::find(admins.begin(), admins.end(), user_id) != admins.end();
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
        }
        
        response = filterCQCodes(response);
        
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
        std::regex cq_regex("\\[CQ:[^\\]]+\\]");
        result = std::regex_replace(result, cq_regex, "");
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
