#pragma once

#include "../core/Types.h"
#include "../core/Event.h"
#include "../api/OneBotApi.h"
#include <string>
#include <memory>
#include <vector>

namespace LCHBOT {

struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    int priority = 0;
};

class PluginContext {
public:
    PluginContext(OneBotApi* api) : api_(api) {}
    
    OneBotApi* getApi() const { return api_; }
    
    void reply(const MessageEvent& event, const std::string& message) {
        if (!api_) return;
        if (event.isGroup()) {
            api_->sendGroupMsg(event.group_id, message);
        } else {
            api_->sendPrivateMsg(event.user_id, message);
        }
    }
    
    void reply(const MessageEvent& event, const std::vector<MessageSegment>& message) {
        if (!api_) return;
        if (event.isGroup()) {
            api_->sendGroupMsg(event.group_id, message);
        } else {
            api_->sendPrivateMsg(event.user_id, message);
        }
    }
    
private:
    OneBotApi* api_;
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    virtual PluginInfo getInfo() const = 0;
    
    virtual bool onLoad(PluginContext* context) { return true; }
    virtual void onUnload() {}
    virtual void onEnable() {}
    virtual void onDisable() {}
    
    virtual bool onMessage(const MessageEvent& event) { return false; }
    virtual bool onNotice(const NoticeEvent& event) { return false; }
    virtual bool onRequest(const RequestEvent& event) { return false; }
    
    virtual bool onPrivateMessage(const MessageEvent& event) { return false; }
    virtual bool onGroupMessage(const MessageEvent& event) { return false; }
    
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
    PluginContext* getContext() const { return context_; }
    void setContext(PluginContext* context) { context_ = context; }
    
protected:
    bool enabled_ = true;
    PluginContext* context_ = nullptr;
};

using PluginCreateFunc = IPlugin* (*)();
using PluginDestroyFunc = void (*)(IPlugin*);

#ifdef _WIN32
#define LCHBOT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define LCHBOT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define LCHBOT_PLUGIN(PluginClass) \
    LCHBOT_PLUGIN_EXPORT LCHBOT::IPlugin* lchbot_plugin_create() { \
        return new PluginClass(); \
    } \
    LCHBOT_PLUGIN_EXPORT void lchbot_plugin_destroy(LCHBOT::IPlugin* plugin) { \
        delete plugin; \
    }

}
