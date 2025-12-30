#pragma once

#include "Plugin.h"
#include "../core/JsonParser.h"
#include "../core/Config.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace LCHBOT {

class PythonInterpreter {
public:
    static PythonInterpreter& instance() {
        static PythonInterpreter inst;
        return inst;
    }
    
    bool initialize(const std::string& python_home = "") {
        if (initialized_) return true;
        
#ifdef _WIN32
        std::vector<std::string> python_versions = {"313", "312", "311", "310", "39", "38"};
        std::vector<std::string> search_paths;
        
        if (!python_home.empty()) {
            search_paths.push_back(python_home);
        }
        
        char* localappdata = nullptr;
        size_t len = 0;
        if (_dupenv_s(&localappdata, &len, "LOCALAPPDATA") == 0 && localappdata) {
            for (const auto& ver : python_versions) {
                search_paths.push_back(std::string(localappdata) + "\\Programs\\Python\\Python" + ver);
            }
            free(localappdata);
        }
        
        char* appdata = nullptr;
        if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
            for (const auto& ver : python_versions) {
                search_paths.push_back(std::string(appdata) + "\\Python\\Python" + ver);
            }
            free(appdata);
        }
        
        for (const auto& ver : python_versions) {
            search_paths.push_back("C:\\Python" + ver);
            search_paths.push_back("C:\\Program Files\\Python" + ver);
            search_paths.push_back("C:\\Program Files (x86)\\Python" + ver);
            search_paths.push_back("D:\\Python" + ver);
            search_paths.push_back("E:\\Python" + ver);
            search_paths.push_back("F:\\Python" + ver);
        }
        
        search_paths.push_back("");
        
        for (const auto& ver : python_versions) {
            std::string dll_name = "python" + ver + ".dll";
            
            for (const auto& path : search_paths) {
                std::string full_path = path.empty() ? dll_name : path + "\\" + dll_name;
                python_module_ = LoadLibraryA(full_path.c_str());
                if (python_module_) {
                    break;
                }
            }
            if (python_module_) break;
        }
        
        if (!python_module_) {
            return false;
        }
        
        Py_Initialize = (Py_InitializeFunc)GetProcAddress(python_module_, "Py_Initialize");
        Py_Finalize = (Py_FinalizeFunc)GetProcAddress(python_module_, "Py_Finalize");
        PyRun_SimpleString = (PyRun_SimpleStringFunc)GetProcAddress(python_module_, "PyRun_SimpleString");
        PyRun_String = (PyRun_StringFunc)GetProcAddress(python_module_, "PyRun_String");
        Py_DecRef = (Py_DecRefFunc)GetProcAddress(python_module_, "Py_DecRef");
        PyErr_Print = (PyErr_PrintFunc)GetProcAddress(python_module_, "PyErr_Print");
        PyErr_Occurred = (PyErr_OccurredFunc)GetProcAddress(python_module_, "PyErr_Occurred");
        PyErr_Clear = (PyErr_ClearFunc)GetProcAddress(python_module_, "PyErr_Clear");
        PyDict_New = (PyDict_NewFunc)GetProcAddress(python_module_, "PyDict_New");
        PyDict_SetItemString = (PyDict_SetItemStringFunc)GetProcAddress(python_module_, "PyDict_SetItemString");
        PyDict_GetItemString = (PyDict_GetItemStringFunc)GetProcAddress(python_module_, "PyDict_GetItemString");
        PyModule_GetDict = (PyModule_GetDictFunc)GetProcAddress(python_module_, "PyModule_GetDict");
        PyImport_AddModule = (PyImport_AddModuleFunc)GetProcAddress(python_module_, "PyImport_AddModule");
        PyImport_ImportModule = (PyImport_ImportModuleFunc)GetProcAddress(python_module_, "PyImport_ImportModule");
        PyUnicode_FromString = (PyUnicode_FromStringFunc)GetProcAddress(python_module_, "PyUnicode_FromString");
        PyUnicode_AsUTF8 = (PyUnicode_AsUTF8Func)GetProcAddress(python_module_, "PyUnicode_AsUTF8");
        PyLong_FromLongLong = (PyLong_FromLongLongFunc)GetProcAddress(python_module_, "PyLong_FromLongLong");
        PyLong_AsLongLong = (PyLong_AsLongLongFunc)GetProcAddress(python_module_, "PyLong_AsLongLong");
        PyBool_FromLong = (PyBool_FromLongFunc)GetProcAddress(python_module_, "PyBool_FromLong");
        PyObject_IsTrue = (PyObject_IsTrueFunc)GetProcAddress(python_module_, "PyObject_IsTrue");
        PyObject_Call = (PyObject_CallFunc)GetProcAddress(python_module_, "PyObject_Call");
        PyObject_CallObject = (PyObject_CallObjectFunc)GetProcAddress(python_module_, "PyObject_CallObject");
        PyObject_GetAttrString = (PyObject_GetAttrStringFunc)GetProcAddress(python_module_, "PyObject_GetAttrString");
        PyObject_SetAttrString = (PyObject_SetAttrStringFunc)GetProcAddress(python_module_, "PyObject_SetAttrString");
        PyTuple_New = (PyTuple_NewFunc)GetProcAddress(python_module_, "PyTuple_New");
        PyTuple_SetItem = (PyTuple_SetItemFunc)GetProcAddress(python_module_, "PyTuple_SetItem");
        PyList_New = (PyList_NewFunc)GetProcAddress(python_module_, "PyList_New");
        PyList_Append = (PyList_AppendFunc)GetProcAddress(python_module_, "PyList_Append");
        PySys_SetPath = (PySys_SetPathFunc)GetProcAddress(python_module_, "PySys_SetPath");
        PyGILState_Ensure = (PyGILState_EnsureFunc)GetProcAddress(python_module_, "PyGILState_Ensure");
        PyGILState_Release = (PyGILState_ReleaseFunc)GetProcAddress(python_module_, "PyGILState_Release");
        PyEval_SaveThread = (PyEval_SaveThreadFunc)GetProcAddress(python_module_, "PyEval_SaveThread");
        PyEval_RestoreThread = (PyEval_RestoreThreadFunc)GetProcAddress(python_module_, "PyEval_RestoreThread");
        PyList_Size = (PyList_SizeFunc)GetProcAddress(python_module_, "PyList_Size");
        PyList_GetItem = (PyList_GetItemFunc)GetProcAddress(python_module_, "PyList_GetItem");
        PyObject_Str = (PyObject_StrFunc)GetProcAddress(python_module_, "PyObject_Str");
        PyObject_Repr = (PyObject_ReprFunc)GetProcAddress(python_module_, "PyObject_Repr");
        
        if (!Py_Initialize || !PyRun_SimpleString) {
            FreeLibrary(python_module_);
            python_module_ = nullptr;
            return false;
        }
#endif
        
        if (Py_Initialize) {
            Py_Initialize();
            initialized_ = true;
            
            PyRun_SimpleString(
                "import sys\n"
                "import os\n"
                "class LCHBotOutput:\n"
                "    def __init__(self, original):\n"
                "        self.original = original\n"
                "        self.buffer = ''\n"
                "    def write(self, text):\n"
                "        self.buffer += text\n"
                "        if self.original:\n"
                "            try:\n"
                "                self.original.write(text)\n"
                "                self.original.flush()\n"
                "            except: pass\n"
                "    def flush(self):\n"
                "        if self.original:\n"
                "            try: self.original.flush()\n"
                "            except: pass\n"
                "    def get_output(self):\n"
                "        result = self.buffer\n"
                "        self.buffer = ''\n"
                "        return result\n"
                "_lchbot_stdout_orig = sys.__stdout__\n"
                "_lchbot_stderr_orig = sys.__stderr__\n"
                "_lchbot_output = LCHBotOutput(_lchbot_stdout_orig)\n"
                "sys.stdout = _lchbot_output\n"
                "sys.stderr = LCHBotOutput(_lchbot_stderr_orig)\n"
            );
            
            if (PyEval_SaveThread) {
                main_thread_state_ = PyEval_SaveThread();
            }
        }
        
        return initialized_;
    }
    
    void finalize() {
        if (initialized_ && Py_Finalize) {
            Py_Finalize();
            initialized_ = false;
        }
        
#ifdef _WIN32
        if (python_module_) {
            FreeLibrary(python_module_);
            python_module_ = nullptr;
        }
#endif
    }
    
    bool isInitialized() const { return initialized_; }
    
    bool executeString(const std::string& code) {
        if (!initialized_) {
            return false;
        }
        if (!PyRun_SimpleString) {
            return false;
        }
        
        int gil_state = 0;
        if (PyGILState_Ensure) {
            gil_state = PyGILState_Ensure();
        }
        
        int result = PyRun_SimpleString(code.c_str());
        if (result != 0 && PyErr_Occurred && PyErr_Print) {
            PyErr_Print();
        }
        
        if (PyGILState_Release) {
            PyGILState_Release(gil_state);
        }
        
        return result == 0;
    }
    
    bool executeFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return executeString(buffer.str());
    }
    
    void addToPath(const std::string& path) {
        if (!initialized_) return;
        std::string cmd = "import sys; sys.path.insert(0, r'" + path + "')";
        executeString(cmd);
    }
    
    std::string getGlobalString(const std::string& var_name) {
        if (!initialized_ || !PyImport_AddModule || !PyModule_GetDict || !PyDict_GetItemString || !PyUnicode_AsUTF8) {
            return "";
        }
        
        int gil_state = 0;
        if (PyGILState_Ensure) {
            gil_state = PyGILState_Ensure();
        }
        
        const char* result = getGlobalStringInternal(var_name.c_str());
        std::string ret = result ? std::string(result) : "";
        
        if (PyGILState_Release) {
            PyGILState_Release(gil_state);
        }
        
        return ret;
    }
    
private:
    const char* getGlobalStringInternal(const char* var_name) {
#ifdef _WIN32
        __try {
#endif
            void* main_module = PyImport_AddModule("__main__");
            if (!main_module) return nullptr;
            
            void* main_dict = PyModule_GetDict(main_module);
            if (!main_dict) return nullptr;
            
            void* py_value = PyDict_GetItemString(main_dict, var_name);
            if (!py_value) return nullptr;
            
            return PyUnicode_AsUTF8(py_value);
#ifdef _WIN32
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
#endif
    }
    
public:
    
private:
    PythonInterpreter() = default;
    ~PythonInterpreter() { finalize(); }
    
    bool initialized_ = false;
    
#ifdef _WIN32
    HMODULE python_module_ = nullptr;
#endif
    
    using Py_InitializeFunc = void (*)();
    using Py_FinalizeFunc = void (*)();
    using PyRun_SimpleStringFunc = int (*)(const char*);
    using PyRun_StringFunc = void* (*)(const char*, int, void*, void*);
    using Py_DecRefFunc = void (*)(void*);
    using PyErr_PrintFunc = void (*)();
    using PyErr_OccurredFunc = void* (*)();
    using PyErr_ClearFunc = void (*)();
    using PyDict_NewFunc = void* (*)();
    using PyDict_SetItemStringFunc = int (*)(void*, const char*, void*);
    using PyDict_GetItemStringFunc = void* (*)(void*, const char*);
    using PyModule_GetDictFunc = void* (*)(void*);
    using PyImport_AddModuleFunc = void* (*)(const char*);
    using PyImport_ImportModuleFunc = void* (*)(const char*);
    using PyUnicode_FromStringFunc = void* (*)(const char*);
    using PyUnicode_AsUTF8Func = const char* (*)(void*);
    using PyLong_FromLongLongFunc = void* (*)(long long);
    using PyLong_AsLongLongFunc = long long (*)(void*);
    using PyBool_FromLongFunc = void* (*)(long);
    using PyObject_IsTrueFunc = int (*)(void*);
    using PyObject_CallFunc = void* (*)(void*, void*, void*);
    using PyObject_CallObjectFunc = void* (*)(void*, void*);
    using PyObject_GetAttrStringFunc = void* (*)(void*, const char*);
    using PyObject_SetAttrStringFunc = int (*)(void*, const char*, void*);
    using PyTuple_NewFunc = void* (*)(size_t);
    using PyTuple_SetItemFunc = int (*)(void*, size_t, void*);
    using PyList_NewFunc = void* (*)(size_t);
    using PyList_AppendFunc = int (*)(void*, void*);
    using PySys_SetPathFunc = void (*)(const wchar_t*);
    using PyGILState_EnsureFunc = int (*)();
    using PyGILState_ReleaseFunc = void (*)(int);
    using PyEval_SaveThreadFunc = void* (*)();
    using PyEval_RestoreThreadFunc = void (*)(void*);
    using PyList_SizeFunc = size_t (*)(void*);
    using PyList_GetItemFunc = void* (*)(void*, size_t);
    using PyObject_StrFunc = void* (*)(void*);
    using PyObject_ReprFunc = void* (*)(void*);
    
    Py_InitializeFunc Py_Initialize = nullptr;
    Py_FinalizeFunc Py_Finalize = nullptr;
    PyRun_SimpleStringFunc PyRun_SimpleString = nullptr;
    PyRun_StringFunc PyRun_String = nullptr;
    Py_DecRefFunc Py_DecRef = nullptr;
    PyErr_PrintFunc PyErr_Print = nullptr;
    PyErr_OccurredFunc PyErr_Occurred = nullptr;
    PyErr_ClearFunc PyErr_Clear = nullptr;
    PyDict_NewFunc PyDict_New = nullptr;
    PyDict_SetItemStringFunc PyDict_SetItemString = nullptr;
    PyDict_GetItemStringFunc PyDict_GetItemString = nullptr;
    PyModule_GetDictFunc PyModule_GetDict = nullptr;
    PyImport_AddModuleFunc PyImport_AddModule = nullptr;
    PyImport_ImportModuleFunc PyImport_ImportModule = nullptr;
    PyUnicode_FromStringFunc PyUnicode_FromString = nullptr;
    PyUnicode_AsUTF8Func PyUnicode_AsUTF8 = nullptr;
    PyLong_FromLongLongFunc PyLong_FromLongLong = nullptr;
    PyLong_AsLongLongFunc PyLong_AsLongLong = nullptr;
    PyBool_FromLongFunc PyBool_FromLong = nullptr;
    PyObject_IsTrueFunc PyObject_IsTrue = nullptr;
    PyObject_CallFunc PyObject_Call = nullptr;
    PyObject_CallObjectFunc PyObject_CallObject = nullptr;
    PyObject_GetAttrStringFunc PyObject_GetAttrString = nullptr;
    PyObject_SetAttrStringFunc PyObject_SetAttrString = nullptr;
    PyTuple_NewFunc PyTuple_New = nullptr;
    PyTuple_SetItemFunc PyTuple_SetItem = nullptr;
    PyList_NewFunc PyList_New = nullptr;
    PyList_AppendFunc PyList_Append = nullptr;
    PySys_SetPathFunc PySys_SetPath = nullptr;
    PyGILState_EnsureFunc PyGILState_Ensure = nullptr;
    PyGILState_ReleaseFunc PyGILState_Release = nullptr;
    PyEval_SaveThreadFunc PyEval_SaveThread = nullptr;
    PyEval_RestoreThreadFunc PyEval_RestoreThread = nullptr;
    void* main_thread_state_ = nullptr;
    PyList_SizeFunc PyList_Size = nullptr;
    PyList_GetItemFunc PyList_GetItem = nullptr;
    PyObject_StrFunc PyObject_Str = nullptr;
    PyObject_ReprFunc PyObject_Repr = nullptr;
};

class PythonPlugin : public IPlugin {
public:
    PythonPlugin(const std::string& script_path) : script_path_(script_path) {
        std::filesystem::path p(script_path);
        info_.name = p.stem().string();
        info_.version = "1.0.0";
        info_.author = "Python";
        info_.description = "Python plugin: " + info_.name;
    }
    
    PluginInfo getInfo() const override { return info_; }
    
    bool onLoad(PluginContext* context) override {
        context_ = context;
        
        auto& py = PythonInterpreter::instance();
        if (!py.isInitialized()) {
            return false;
        }
        
        std::filesystem::path p(script_path_);
        py.addToPath(p.parent_path().string());
        
        std::ifstream file(script_path_);
        if (!file.is_open()) return false;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        script_content_ = buffer.str();
        
        std::string master_list = "[";
        auto& masters = ConfigManager::instance().config().master_qq;
        for (size_t i = 0; i < masters.size(); ++i) {
            if (i > 0) master_list += ",";
            master_list += std::to_string(masters[i]);
        }
        master_list += "]";
        
        std::string init_code = 
            "if '_lchbot_plugins' not in globals():\n"
            "    _lchbot_plugins = {}\n"
            "_lchbot_reply_queue = []\n"
            "_lchbot_master_qq = " + master_list + "\n"
            "\n"
            "class LCHBotPlugin:\n"
            "    def __init__(self):\n"
            "        self.name = '" + info_.name + "'\n"
            "        self.version = '1.0.0'\n"
            "        self.author = 'Python'\n"
            "        self.description = ''\n"
            "        self.priority = 50\n"
            "    def on_load(self): pass\n"
            "    def on_unload(self): pass\n"
            "    def on_enable(self): pass\n"
            "    def on_disable(self): pass\n"
            "    def on_message(self, event): return False\n"
            "    def on_private_message(self, event): return False\n"
            "    def on_group_message(self, event): return False\n"
            "    def on_notice(self, event): return False\n"
            "    def on_request(self, event): return False\n"
            "    def is_master(self, user_id):\n"
            "        return int(user_id) in _lchbot_master_qq\n"
            "    def get_masters(self):\n"
            "        return _lchbot_master_qq\n"
            "    def reply(self, event, message):\n"
            "        global _lchbot_reply_queue\n"
            "        msg_type = event.get('message_type', 'private')\n"
            "        if msg_type == 'group':\n"
            "            _lchbot_reply_queue.append({'action': 'send_group_msg', 'group_id': event.get('group_id', 0), 'message': message})\n"
            "        else:\n"
            "            _lchbot_reply_queue.append({'action': 'send_private_msg', 'user_id': event.get('user_id', 0), 'message': message})\n"
            "    def send_group_msg(self, group_id, message):\n"
            "        global _lchbot_reply_queue\n"
            "        _lchbot_reply_queue.append({'action': 'send_group_msg', 'group_id': group_id, 'message': message})\n"
            "    def send_private_msg(self, user_id, message):\n"
            "        global _lchbot_reply_queue\n"
            "        _lchbot_reply_queue.append({'action': 'send_private_msg', 'user_id': user_id, 'message': message})\n"
            "\n"
            "_lchbot_current_plugin_name = '" + info_.name + "'\n"
            "\n"
            "def register_plugin(plugin):\n"
            "    global _lchbot_plugins, _lchbot_current_plugin_name\n"
            "    _lchbot_plugins[_lchbot_current_plugin_name] = plugin\n"
            "\n";
        
        py.executeString(init_code);
        
        if (!py.executeString(script_content_)) {
            return false;
        }
        
        std::string original_name = info_.name;
        
        py.executeString(
            "if '" + original_name + "' in _lchbot_plugins:\n"
            "    _lchbot_plugins['" + original_name + "'].on_load()\n"
        );
        
        updatePluginInfo();
        
        py.executeString(
            "if '" + original_name + "' in _lchbot_plugins and '" + info_.name + "' not in _lchbot_plugins:\n"
            "    _lchbot_plugins['" + info_.name + "'] = _lchbot_plugins['" + original_name + "']\n"
            "    print(f'[Plugin] Remapped {'" + original_name + "'} -> {'" + info_.name + "'}')\n"
        );
        
        loaded_ = true;
        return true;
    }
    
    void updatePluginInfo() {
        auto& py = PythonInterpreter::instance();
        
        std::string get_info_code = 
            "_lchbot_tmp_name = ''\n"
            "_lchbot_tmp_version = ''\n"
            "_lchbot_tmp_author = ''\n"
            "_lchbot_tmp_desc = ''\n"
            "if '" + info_.name + "' in _lchbot_plugins:\n"
            "    _p = _lchbot_plugins['" + info_.name + "']\n"
            "    _lchbot_tmp_name = str(getattr(_p, 'name', ''))\n"
            "    _lchbot_tmp_version = str(getattr(_p, 'version', '1.0.0'))\n"
            "    _lchbot_tmp_author = str(getattr(_p, 'author', 'Unknown'))\n"
            "    _lchbot_tmp_desc = str(getattr(_p, 'description', ''))\n";
        py.executeString(get_info_code);
        
        std::string name = py.getGlobalString("_lchbot_tmp_name");
        std::string version = py.getGlobalString("_lchbot_tmp_version");
        std::string author = py.getGlobalString("_lchbot_tmp_author");
        std::string desc = py.getGlobalString("_lchbot_tmp_desc");
        
        if (!name.empty()) info_.name = name;
        if (!version.empty()) info_.version = version;
        if (!author.empty()) info_.author = author;
        if (!desc.empty()) info_.description = desc;
    }
    
    void onUnload() override {
        if (loaded_) {
            auto& py = PythonInterpreter::instance();
            py.executeString(
                "if '" + info_.name + "' in _lchbot_plugins:\n"
                "    _lchbot_plugins['" + info_.name + "'].on_unload()\n"
            );
            loaded_ = false;
        }
    }
    
    void onEnable() override {
        if (loaded_) {
            auto& py = PythonInterpreter::instance();
            py.executeString(
                "if '" + info_.name + "' in _lchbot_plugins:\n"
                "    _lchbot_plugins['" + info_.name + "'].on_enable()\n"
            );
        }
    }
    
    void onDisable() override {
        if (loaded_) {
            auto& py = PythonInterpreter::instance();
            py.executeString(
                "if '" + info_.name + "' in _lchbot_plugins:\n"
                "    _lchbot_plugins['" + info_.name + "'].on_disable()\n"
            );
        }
    }
    
    bool onMessage(const MessageEvent& event) override {
        if (!loaded_) return false;
        
        try {
            std::string event_json = createEventJson(event);
            std::string escaped_json = escapeForPython(event_json);
            
            auto& py = PythonInterpreter::instance();
            
            std::string code = 
                "import json\n"
                "_lchbot_reply_queue = []\n"
                "try:\n"
                "    _lchbot_event = json.loads(" + escaped_json + ")\n"
                "    if '" + info_.name + "' in _lchbot_plugins:\n"
                "        _lchbot_result = _lchbot_plugins['" + info_.name + "'].on_message(_lchbot_event) or False\n"
                "except Exception as e:\n"
                "    import traceback\n"
                "    print(f'[Plugin:" + info_.name + "] {traceback.format_exc()}')\n";
            
            py.executeString(code);
            
            if (context_) {
                auto replies = getReplyQueue();
                for (const auto& reply : replies) {
                    if (reply.is_group) {
                        context_->getApi()->sendGroupMsg(reply.target_id, reply.message);
                    } else {
                        context_->getApi()->sendPrivateMsg(reply.target_id, reply.message);
                    }
                }
            }
        } catch (...) {
        }
        
        return false;
    }
    
    bool onPrivateMessage(const MessageEvent& event) override {
        if (!loaded_) return false;
        
        try {
            std::string event_json = createEventJson(event);
            std::string escaped_json = escapeForPython(event_json);
            
            auto& py = PythonInterpreter::instance();
            std::string code = 
                "import json\n"
                "try:\n"
                "    _lchbot_event = json.loads(" + escaped_json + ")\n"
                "    if '" + info_.name + "' in _lchbot_plugins:\n"
                "        _lchbot_plugins['" + info_.name + "'].on_private_message(_lchbot_event)\n"
                "except Exception as e:\n"
                "    print(f'Plugin error: {e}')\n";
            
            py.executeString(code);
        } catch (...) {
        }
        
        return false;
    }
    
    bool onGroupMessage(const MessageEvent& event) override {
        if (!loaded_) return false;
        
        try {
            std::string event_json = createEventJson(event);
            std::string escaped_json = escapeForPython(event_json);
            
            auto& py = PythonInterpreter::instance();
            std::string code = 
                "import json\n"
                "try:\n"
                "    _lchbot_event = json.loads(" + escaped_json + ")\n"
                "    if '" + info_.name + "' in _lchbot_plugins:\n"
                "        _lchbot_plugins['" + info_.name + "'].on_group_message(_lchbot_event)\n"
                "except Exception as e:\n"
                "    print(f'Plugin error: {e}')\n";
            
            py.executeString(code);
        } catch (...) {
        }
        
        return false;
    }
    
private:
    std::string escapeForPython(const std::string& json) {
        std::string result = "\"";
        for (size_t i = 0; i < json.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(json[i]);
            if (c == '\\') {
                result += "\\\\";
            } else if (c == '"') {
                result += "\\\"";
            } else if (c == '\n') {
                result += "\\n";
            } else if (c == '\r') {
                result += "\\r";
            } else if (c == '\t') {
                result += "\\t";
            } else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                result += buf;
            } else {
                result += static_cast<char>(c);
            }
        }
        result += "\"";
        return result;
    }
    
    std::string createEventJson(const MessageEvent& event) {
        std::map<std::string, JsonValue> obj;
        obj["message_type"] = JsonValue(event.isGroup() ? "group" : "private");
        obj["sub_type"] = JsonValue(event.sub_type);
        obj["message_id"] = JsonValue(static_cast<int64_t>(event.message_id));
        obj["user_id"] = JsonValue(event.user_id);
        obj["group_id"] = JsonValue(event.group_id);
        obj["raw_message"] = JsonValue(event.raw_message);
        obj["time"] = JsonValue(event.time);
        obj["self_id"] = JsonValue(event.self_id);
        
        std::map<std::string, JsonValue> sender;
        sender["user_id"] = JsonValue(event.sender.user_id);
        sender["nickname"] = JsonValue(event.sender.nickname);
        sender["card"] = JsonValue(event.sender.card);
        sender["role"] = JsonValue(event.sender.role);
        obj["sender"] = JsonValue(sender);
        
        std::vector<JsonValue> message;
        for (const auto& seg : event.message) {
            std::map<std::string, JsonValue> seg_obj;
            seg_obj["type"] = JsonValue(seg.type);
            std::map<std::string, JsonValue> data;
            for (const auto& [k, v] : seg.data) {
                data[k] = JsonValue(v);
            }
            seg_obj["data"] = JsonValue(data);
            message.push_back(JsonValue(seg_obj));
        }
        obj["message"] = JsonValue(message);
        
        return JsonParser::stringify(JsonValue(obj));
    }
    
    struct ReplyInfo {
        bool is_group;
        int64_t target_id;
        std::string message;
    };
    
    std::vector<ReplyInfo> getReplyQueue() {
        std::vector<ReplyInfo> replies;
        
        auto& py = PythonInterpreter::instance();
        
        std::string code = 
            "import json\n"
            "_lchbot_reply_json = json.dumps(_lchbot_reply_queue) if _lchbot_reply_queue else '[]'\n"
            "_lchbot_reply_queue = []\n";
        
        py.executeString(code);
        
        std::string json_str = py.getGlobalString("_lchbot_reply_json");
        
        if (!json_str.empty() && json_str != "[]") {
            JsonValue arr = JsonParser::parse(json_str);
            if (arr.isArray()) {
                for (const auto& item : arr.asArray()) {
                    if (item.isObject()) {
                        ReplyInfo info;
                        auto& obj = item.asObject();
                        std::string action = obj.count("action") ? obj.at("action").asString() : "";
                        info.is_group = (action == "send_group_msg");
                        info.target_id = obj.count("group_id") ? obj.at("group_id").asInt() : 
                                       (obj.count("user_id") ? obj.at("user_id").asInt() : 0);
                        info.message = obj.count("message") ? obj.at("message").asString() : "";
                        if (info.target_id > 0 && !info.message.empty()) {
                            replies.push_back(info);
                        }
                    }
                }
            }
        }
        
        return replies;
    }
    
    std::string script_path_;
    std::string script_content_;
    PluginInfo info_;
    bool loaded_ = false;
    PluginContext* context_ = nullptr;
};

}
