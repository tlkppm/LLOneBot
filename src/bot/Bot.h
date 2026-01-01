#pragma once

#include "../core/Types.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "../core/Event.h"
#include "../core/JsonParser.h"
#include "../network/WebSocketServer.h"
#include "../network/WebSocketClient.h"
#include "../api/OneBotApi.h"
#include "../plugin/Plugin.h"
#include "../plugin/PluginManager.h"
#include "../plugin/PythonPlugin.h"
#include "../plugin/AIPlugin.h"
#include "../ai/ContextDatabase.h"
#include "../ai/PersonalitySystem.h"
#include "../admin/AdminServer.h"
#include "../admin/AdminApi.h"
#include "../admin/Statistics.h"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "../core/GroupMemberCache.h"
#include "../core/FileMessageQueue.h"

namespace LCHBOT {

class Bot {
public:
    static Bot& instance() {
        static Bot inst;
        return inst;
    }
    
    bool initialize(const std::string& config_path = "config.ini") {
        LOG_INFO("Initializing LCHBOT...");
        
        auto& config_mgr = ConfigManager::instance();
        if (!config_mgr.load(config_path)) {
            LOG_ERROR("Failed to load configuration");
            return false;
        }
        
        const auto& config = config_mgr.config();
        
        auto& logger = Logger::instance();
        logger.init(
            config.log.log_dir,
            config.log.log_level,
            config.log.console_output,
            config.log.file_output,
            config.log.max_file_size,
            config.log.max_files
        );
        
        if (config.plugin.enable_python) {
            LOG_INFO("Initializing Python interpreter...");
            auto& py = PythonInterpreter::instance();
            if (!py.initialize(config.plugin.python_home)) {
                LOG_WARN("Failed to initialize Python interpreter, Python plugins will be disabled");
            } else {
                LOG_INFO("Python interpreter initialized");
                PythonTaskQueue::instance().start(4);
            }
        }
        
        ContextDatabase::instance().initialize("data/context.db");
        PersonalitySystem::instance().initialize();
        
        api_ = std::make_unique<OneBotApi>();
        PythonTaskQueue::instance().setApi(api_.get());
        context_ = std::make_unique<PluginContext>(api_.get());
        
        auto& plugin_mgr = PluginManager::instance();
        plugin_mgr.setContext(context_.get());
        
        plugin_mgr.registerBuiltinPlugin<AIPlugin>();
        
        LOG_INFO("Loading plugins from: " + config.plugin.plugins_dir);
        plugin_mgr.loadPluginsFromDirectory(
            config.plugin.plugins_dir,
            config.plugin.enable_python,
            config.plugin.enable_native
        );
        
        auto plugin_list = plugin_mgr.getPluginList();
        LOG_INFO("Loaded " + std::to_string(plugin_list.size()) + " plugin(s)");
        for (const auto& info : plugin_list) {
            LOG_INFO("  - " + info.name + " v" + info.version + " [" + (plugin_mgr.isPluginEnabled(info.name) ? "enabled" : "disabled") + "]");
        }
        
        ws_client_ = std::make_unique<WebSocketClient>();
        
        ws_client_->setConnectCallback([this]() {
            LOG_INFO("Connected to LLBot");
            connected_ = true;
            api_->getLoginInfo();
        });
        
        ws_client_->setDisconnectCallback([this]() {
            LOG_WARN("Disconnected from LLBot");
            connected_ = false;
            if (running_) {
                scheduleReconnect();
            }
        });
        
        ws_client_->setMessageCallback([this](const std::string& message) {
            handleMessage(0, message);
        });
        
        ws_client_->setErrorCallback([this](const std::string& error) {
            LOG_ERROR("WebSocket error: " + error);
        });
        
        api_->setSendFunction([this](const std::string& message) {
            if (ws_client_ && ws_client_->isConnected()) {
                ws_client_->send(message);
            }
        });
        
        initialized_ = true;
        LOG_INFO("LCHBOT initialized successfully");
        
        lchbot::FileMessageQueue::instance().setSendGroupCallback([this](const std::string& msg, int64_t group_id) {
            if (api_) api_->sendGroupMsg(group_id, msg);
        });
        lchbot::FileMessageQueue::instance().setSendPrivateCallback([this](const std::string& msg, int64_t user_id) {
            if (api_) api_->sendPrivateMsg(user_id, msg);
        });
        lchbot::FileMessageQueue::instance().start();
        
        AdminApi::instance().initialize();
        if (AdminServer::instance().start(config.admin_port > 0 ? config.admin_port : 8080)) {
            LOG_INFO("[Admin] Management panel: http://127.0.0.1:" + std::to_string(config.admin_port > 0 ? config.admin_port : 8080));
        }
        
        PluginManager::instance().startHotReload(5);
        
        return true;
    }
    
    bool start() {
        if (!initialized_) {
            LOG_ERROR("Bot not initialized");
            return false;
        }
        
        running_ = true;
        connectToLLBot();
        
        return true;
    }
    
    void connectToLLBot() {
        const auto& config = ConfigManager::instance().config();
        
        LOG_INFO("Connecting to LLBot at ws://" + config.websocket.host + ":" + std::to_string(config.websocket.port) + config.websocket.path);
        
        if (ws_client_->connect(config.websocket.host, config.websocket.port, config.websocket.path)) {
            LOG_INFO("Connected to LLBot successfully");
        } else {
            LOG_ERROR("Failed to connect to LLBot, will retry...");
            scheduleReconnect();
        }
    }
    
    void scheduleReconnect() {
        if (!running_) return;
        
        const auto& config = ConfigManager::instance().config();
        
        std::thread([this, interval = config.websocket.reconnect_interval]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            if (running_ && !connected_) {
                LOG_INFO("Attempting to reconnect...");
                connectToLLBot();
            }
        }).detach();
    }
    
    void run() {
        if (!running_) {
            if (!start()) return;
        }
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void stop() {
        LOG_INFO("Stopping LCHBOT...");
        
        running_ = false;
        
        PluginManager::instance().stopHotReload();
        AdminServer::instance().stop();
        
        if (ws_client_) {
            ws_client_->disconnect();
        }
        
        PluginManager::instance().unloadAllPlugins();
        
        if (ConfigManager::instance().config().plugin.enable_python) {
            PythonInterpreter::instance().finalize();
        }
        
        Logger::instance().shutdown();
        
        LOG_INFO("LCHBOT stopped");
    }
    
    bool isRunning() const { return running_; }
    
    OneBotApi* getApi() { return api_.get(); }
    
    void enablePlugin(const std::string& name) {
        if (PluginManager::instance().enablePlugin(name)) {
            LOG_INFO("[Plugin] Enabled: " + name);
        }
    }
    
    void disablePlugin(const std::string& name) {
        if (PluginManager::instance().disablePlugin(name)) {
            LOG_INFO("[Plugin] Disabled: " + name);
        }
    }
    
    void listPlugins() {
        auto& mgr = PluginManager::instance();
        auto list = mgr.getPluginList();
        LOG_INFO("=== Plugin List (" + std::to_string(list.size()) + ") ===");
        for (const auto& info : list) {
            std::string status = mgr.isPluginEnabled(info.name) ? "enabled" : "disabled";
            LOG_INFO("  " + info.name + " v" + info.version + " [" + status + "]");
        }
    }
    
    bool reloadPlugin(const std::string& name) {
        auto& mgr = PluginManager::instance();
        if (mgr.unloadPlugin(name)) {
            LOG_INFO("[Plugin] Unloaded: " + name);
        }
        return true;
    }
    
    void sendGroupMessage(int64_t group_id, const std::string& message) {
        if (api_) {
            api_->sendGroupMsg(group_id, message);
        }
    }
    
    void sendPrivateMessage(int64_t user_id, const std::string& message) {
        if (api_) {
            api_->sendPrivateMsg(user_id, message);
        }
    }
    
private:
    Bot() = default;
    ~Bot() { stop(); }
    
    void handleMessage(int client_id, const std::string& message) {
        try {
            JsonValue json = JsonParser::parse(message);
            
            if (!json.isObject()) return;
            const auto& obj = json.asObject();
            
            if (obj.find("echo") != obj.end()) {
                api_->handleResponse(json);
                return;
            }
            
            auto event = EventParser::parse(json);
            if (!event) return;
            
            auto& plugin_mgr = PluginManager::instance();
            
            switch (event->type) {
                case EventType::Message: {
                    auto* msg_event = static_cast<MessageEvent*>(event.get());
                    std::string sender_name = msg_event->sender.card.empty() ? msg_event->sender.nickname : msg_event->sender.card;
                    if (msg_event->isGroup()) {
                        LOG_MSG("[Group:" + std::to_string(msg_event->group_id) + "] " + sender_name + "(" + std::to_string(msg_event->user_id) + "): " + msg_event->raw_message);
                        
                        fetchGroupMembersIfNeeded(msg_event->group_id);
                        
                        std::string context_key = "g_" + std::to_string(msg_event->group_id);
                        ContextDatabase::instance().addMessage(
                            context_key, 
                            "user", 
                            msg_event->raw_message, 
                            sender_name, 
                            msg_event->user_id
                        );
                    } else {
                        LOG_MSG("[Private] " + sender_name + "(" + std::to_string(msg_event->user_id) + "): " + msg_event->raw_message);
                    }
                    plugin_mgr.dispatchMessage(*msg_event);
                    break;
                }
                case EventType::Notice: {
                    auto* notice_event = static_cast<NoticeEvent*>(event.get());
                    plugin_mgr.dispatchNotice(*notice_event);
                    break;
                }
                case EventType::Request: {
                    auto* request_event = static_cast<RequestEvent*>(event.get());
                    plugin_mgr.dispatchRequest(*request_event);
                    break;
                }
                case EventType::Meta: {
                    auto* meta_event = static_cast<MetaEvent*>(event.get());
                    if (meta_event->meta_event_type == MetaEventType::Lifecycle) {
                        LOG_INFO("Lifecycle event: " + meta_event->sub_type);
                    } else if (meta_event->meta_event_type == MetaEventType::Heartbeat) {
                    }
                    break;
                }
                default:
                    break;
            }
            
            EventDispatcher::instance().dispatch(*event);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to handle message: " + std::string(e.what()));
        }
    }
    
    void fetchGroupMembersIfNeeded(int64_t group_id) {
        auto& cache = GroupMemberCache::instance();
        if (cache.hasGroup(group_id) || cache.isPending(group_id)) return;
        
        cache.markPending(group_id);
        
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        
        api_->callApiWithCallback("get_group_member_list", JsonValue(params),
            [group_id](const ApiResponse& member_resp) {
                if (member_resp.retcode != 0 || !member_resp.data.isArray()) return;
                
                std::vector<std::pair<int64_t, std::string>> members;
                for (const auto& member : member_resp.data.asArray()) {
                    if (!member.isObject()) continue;
                    auto& m = member.asObject();
                    int64_t uid = m.count("user_id") ? m.at("user_id").asInt() : 0;
                    std::string nick = "";
                    if (m.count("card") && !m.at("card").asString().empty()) {
                        nick = m.at("card").asString();
                    } else if (m.count("nickname")) {
                        nick = m.at("nickname").asString();
                    }
                    if (uid > 0 && !nick.empty()) {
                        members.emplace_back(uid, nick);
                    }
                }
                GroupMemberCache::instance().setMembers(group_id, members);
                LOG_INFO("[Bot] Cached " + std::to_string(members.size()) + " members for group " + std::to_string(group_id));
            });
    }
    
    std::unique_ptr<WebSocketClient> ws_client_;
    std::unique_ptr<OneBotApi> api_;
    std::unique_ptr<PluginContext> context_;
    
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
};

}
