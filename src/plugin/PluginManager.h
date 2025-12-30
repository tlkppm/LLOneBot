#pragma once

#include "Plugin.h"
#include "PythonPlugin.h"
#include "../core/Logger.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace LCHBOT {

class PluginManager {
public:
    static PluginManager& instance() {
        static PluginManager inst;
        return inst;
    }
    
    void setContext(PluginContext* context) {
        context_ = context;
    }
    
    bool loadPluginsFromDirectory(const std::string& directory, bool enable_python = true, bool enable_native = true) {
        if (!std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
            return true;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (enable_python && ext == ".py") {
                loadPythonPlugin(entry.path().string());
            }
#ifdef _WIN32
            else if (enable_native && ext == ".dll") {
                loadNativePlugin(entry.path().string());
            }
#else
            else if (enable_native && ext == ".so") {
                loadNativePlugin(entry.path().string());
            }
#endif
        }
        
        sortPluginsByPriority();
        return true;
    }
    
    bool loadPythonPlugin(const std::string& path) {
        auto& py = PythonInterpreter::instance();
        if (!py.isInitialized()) {
            LOG_WARN("Python interpreter not initialized, skipping plugin: " + path);
            return false;
        }
        
        auto plugin = std::make_unique<PythonPlugin>(path);
        auto pre_info = plugin->getInfo();
        
        if (isPluginLoaded(pre_info.name)) {
            LOG_WARN("Plugin already loaded: " + pre_info.name);
            return false;
        }
        
        plugin->setContext(context_);
        if (!plugin->onLoad(context_)) {
            LOG_ERROR("Failed to load Python plugin: " + path);
            return false;
        }
        
        auto info = plugin->getInfo();
        
        if (isPluginLoaded(info.name)) {
            LOG_WARN("Plugin already loaded: " + info.name);
            return false;
        }
        
        LOG_INFO("[Plugin] Loaded: " + info.name + " v" + info.version + " by " + info.author);
        plugins_[info.name] = std::move(plugin);
        loaded_plugin_paths_.insert(path);
        sortPluginsByPriority();
        return true;
    }
    
    bool loadNativePlugin(const std::string& path) {
#ifdef _WIN32
        HMODULE handle = LoadLibraryA(path.c_str());
        if (!handle) {
            LOG_ERROR("Failed to load native plugin: " + path);
            return false;
        }
        
        auto create_func = (PluginCreateFunc)GetProcAddress(handle, "lchbot_plugin_create");
        auto destroy_func = (PluginDestroyFunc)GetProcAddress(handle, "lchbot_plugin_destroy");
        
        if (!create_func || !destroy_func) {
            LOG_ERROR("Invalid plugin interface: " + path);
            FreeLibrary(handle);
            return false;
        }
#else
        void* handle = dlopen(path.c_str(), RTLD_NOW);
        if (!handle) {
            LOG_ERROR("Failed to load native plugin: " + path + " - " + dlerror());
            return false;
        }
        
        auto create_func = (PluginCreateFunc)dlsym(handle, "lchbot_plugin_create");
        auto destroy_func = (PluginDestroyFunc)dlsym(handle, "lchbot_plugin_destroy");
        
        if (!create_func || !destroy_func) {
            LOG_ERROR("Invalid plugin interface: " + path);
            dlclose(handle);
            return false;
        }
#endif
        
        IPlugin* raw_plugin = create_func();
        if (!raw_plugin) {
            LOG_ERROR("Failed to create plugin instance: " + path);
#ifdef _WIN32
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            return false;
        }
        
        auto info = raw_plugin->getInfo();
        
        if (isPluginLoaded(info.name)) {
            LOG_WARN("Plugin already loaded: " + info.name);
            destroy_func(raw_plugin);
#ifdef _WIN32
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            return false;
        }
        
        raw_plugin->setContext(context_);
        if (!raw_plugin->onLoad(context_)) {
            LOG_ERROR("Failed to load native plugin: " + path);
            destroy_func(raw_plugin);
#ifdef _WIN32
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            return false;
        }
        
        LOG_INFO("Loaded native plugin: " + info.name + " v" + info.version);
        
        NativePluginData data;
        data.handle = handle;
        data.destroy_func = destroy_func;
        native_plugins_[info.name] = data;
        
        plugins_[info.name] = std::unique_ptr<IPlugin>(raw_plugin);
        return true;
    }
    
    bool unloadPlugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) {
            return false;
        }
        
        it->second->onUnload();
        
        auto native_it = native_plugins_.find(name);
        if (native_it != native_plugins_.end()) {
            native_it->second.destroy_func(it->second.release());
#ifdef _WIN32
            FreeLibrary((HMODULE)native_it->second.handle);
#else
            dlclose(native_it->second.handle);
#endif
            native_plugins_.erase(native_it);
        }
        
        plugins_.erase(it);
        LOG_INFO("Unloaded plugin: " + name);
        return true;
    }
    
    void unloadAllPlugins() {
        for (auto& [name, plugin] : plugins_) {
            plugin->onUnload();
            
            auto native_it = native_plugins_.find(name);
            if (native_it != native_plugins_.end()) {
                native_it->second.destroy_func(plugin.release());
#ifdef _WIN32
                FreeLibrary((HMODULE)native_it->second.handle);
#else
                dlclose(native_it->second.handle);
#endif
            }
        }
        
        plugins_.clear();
        native_plugins_.clear();
        sorted_plugins_.clear();
    }
    
    bool enablePlugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) return false;
        
        if (!it->second->isEnabled()) {
            it->second->setEnabled(true);
            it->second->onEnable();
            LOG_INFO("Enabled plugin: " + name);
        }
        return true;
    }
    
    bool disablePlugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) return false;
        
        if (it->second->isEnabled()) {
            it->second->onDisable();
            it->second->setEnabled(false);
            LOG_INFO("Disabled plugin: " + name);
        }
        return true;
    }
    
    bool isPluginLoaded(const std::string& name) const {
        return plugins_.find(name) != plugins_.end();
    }
    
    template<typename T>
    bool registerBuiltinPlugin() {
        auto plugin = std::make_unique<T>();
        auto info = plugin->getInfo();
        
        if (isPluginLoaded(info.name)) {
            LOG_WARN("[Plugin] Builtin already loaded: " + info.name);
            return false;
        }
        
        plugin->setContext(context_);
        if (!plugin->onLoad(context_)) {
            LOG_ERROR("[Plugin] Builtin load failed: " + info.name);
            return false;
        }
        
        LOG_INFO("[Plugin] Builtin loaded: " + info.name + " v" + info.version);
        plugins_[info.name] = std::move(plugin);
        sortPluginsByPriority();
        return true;
    }
    
    bool isPluginEnabled(const std::string& name) const {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) return false;
        return it->second->isEnabled();
    }
    
    void reloadPythonPlugins() {
        std::string plugins_dir = "plugins";
        if (!std::filesystem::exists(plugins_dir)) return;
        
        for (const auto& entry : std::filesystem::directory_iterator(plugins_dir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".py") {
                std::string path = entry.path().string();
                std::string name = entry.path().stem().string();
                
                if (name.front() == '_') continue;
                
                auto current_mod_time = std::filesystem::last_write_time(entry.path());
                auto it = plugin_mod_times_.find(path);
                
                bool need_reload = false;
                if (it == plugin_mod_times_.end()) {
                    need_reload = true;
                    LOG_INFO("[HotReload] New plugin detected: " + name);
                } else if (it->second != current_mod_time) {
                    need_reload = true;
                    LOG_INFO("[HotReload] Plugin modified: " + name);
                }
                
                if (need_reload) {
                    if (reloadSinglePythonPlugin(path, name)) {
                        plugin_mod_times_[path] = current_mod_time;
                        loaded_plugin_paths_.insert(path);
                        LOG_INFO("[HotReload] Successfully reloaded: " + name);
                    }
                }
            }
        }
    }
    
    bool reloadSinglePythonPlugin(const std::string& path, const std::string& name) {
        auto& py = PythonInterpreter::instance();
        if (!py.isInitialized()) return false;
        
        std::ifstream file(path);
        if (!file.is_open()) return false;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string code = buffer.str();
        file.close();
        
        std::string reload_code = 
            "import sys\n"
            "import importlib\n"
            "_reload_path = r'" + path + "'\n"
            "_reload_name = '" + name + "'\n"
            "if _reload_name in _lchbot_plugins:\n"
            "    try:\n"
            "        del _lchbot_plugins[_reload_name]\n"
            "    except: pass\n"
            "for mod_name in list(sys.modules.keys()):\n"
            "    if _reload_name in mod_name:\n"
            "        try:\n"
            "            del sys.modules[mod_name]\n"
            "        except: pass\n";
        
        py.executeString(reload_code);
        
        std::string exec_code = 
            "try:\n"
            "    exec(open(r'" + path + "', encoding='utf-8').read())\n"
            "    print(f'[HotReload] Plugin {\"" + name + "\"} reloaded successfully')\n"
            "except Exception as e:\n"
            "    import traceback\n"
            "    print(f'[HotReload] Error reloading " + name + ": {e}')\n"
            "    traceback.print_exc()\n";
        
        py.executeString(exec_code);
        return true;
    }
    
    void startHotReload(int interval_seconds = 5) {
        if (hot_reload_running_) return;
        hot_reload_running_ = true;
        
        hot_reload_thread_ = std::thread([this, interval_seconds]() {
            while (hot_reload_running_) {
                std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
                if (hot_reload_running_) {
                    reloadPythonPlugins();
                }
            }
        });
        
        LOG_INFO("[Plugin] Hot reload started (interval: " + std::to_string(interval_seconds) + "s)");
    }
    
    void stopHotReload() {
        hot_reload_running_ = false;
        if (hot_reload_thread_.joinable()) {
            hot_reload_thread_.join();
        }
        LOG_INFO("[Plugin] Hot reload stopped");
    }
    
    IPlugin* getPlugin(const std::string& name) {
        auto it = plugins_.find(name);
        return it != plugins_.end() ? it->second.get() : nullptr;
    }
    
    std::vector<PluginInfo> getPluginList() const {
        std::vector<PluginInfo> list;
        for (const auto& [name, plugin] : plugins_) {
            list.push_back(plugin->getInfo());
        }
        return list;
    }
    
    bool dispatchMessage(const MessageEvent& event) {
        for (auto* plugin : sorted_plugins_) {
            if (!plugin->isEnabled()) continue;
            
            try {
                if (plugin->onMessage(event)) return true;
                
                if (event.isPrivate()) {
                    if (plugin->onPrivateMessage(event)) return true;
                } else {
                    if (plugin->onGroupMessage(event)) return true;
                }
            } catch (...) {
                LOG_ERROR("Exception in plugin: " + plugin->getInfo().name);
            }
        }
        return false;
    }
    
    bool dispatchNotice(const NoticeEvent& event) {
        for (auto* plugin : sorted_plugins_) {
            if (!plugin->isEnabled()) continue;
            
            try {
                if (plugin->onNotice(event)) return true;
            } catch (...) {
                LOG_ERROR("Exception in plugin: " + plugin->getInfo().name);
            }
        }
        return false;
    }
    
    bool dispatchRequest(const RequestEvent& event) {
        for (auto* plugin : sorted_plugins_) {
            if (!plugin->isEnabled()) continue;
            
            try {
                if (plugin->onRequest(event)) return true;
            } catch (...) {
                LOG_ERROR("Exception in plugin: " + plugin->getInfo().name);
            }
        }
        return false;
    }
    
private:
    PluginManager() = default;
    ~PluginManager() { unloadAllPlugins(); }
    
    void sortPluginsByPriority() {
        sorted_plugins_.clear();
        for (auto& [name, plugin] : plugins_) {
            sorted_plugins_.push_back(plugin.get());
        }
        
        std::sort(sorted_plugins_.begin(), sorted_plugins_.end(),
            [](IPlugin* a, IPlugin* b) {
                return a->getInfo().priority > b->getInfo().priority;
            });
    }
    
    struct NativePluginData {
        void* handle = nullptr;
        PluginDestroyFunc destroy_func = nullptr;
    };
    
    std::map<std::string, std::unique_ptr<IPlugin>> plugins_;
    std::map<std::string, NativePluginData> native_plugins_;
    std::vector<IPlugin*> sorted_plugins_;
    PluginContext* context_ = nullptr;
    
    std::atomic<bool> hot_reload_running_{false};
    std::thread hot_reload_thread_;
    std::set<std::string> loaded_plugin_paths_;
    std::map<std::string, std::filesystem::file_time_type> plugin_mod_times_;
};

}
