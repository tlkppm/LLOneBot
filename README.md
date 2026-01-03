# LCHBOT - QQ Bot Framework

<p align="center">
  <b>轻量级、高性能的QQ机器人框架</b><br>
  <b>Lightweight, High-Performance QQ Bot Framework</b>
</p>

---

## 简介 | Introduction

**LCHBOT** 是一个基于 OneBot 11 协议的现代化 QQ 机器人框架，采用 C++ 编写核心，支持 Python 插件热加载，提供企业级的稳定性和扩展性。

**LCHBOT** is a modern QQ bot framework based on the OneBot 11 protocol. Written in C++ for the core with Python plugin hot-reload support, it provides enterprise-level stability and extensibility.

## 特性 | Features

### 核心特性 | Core Features
- **OneBot 11 协议支持** | OneBot 11 Protocol Support
- **WebSocket 连接** | WebSocket Connection
- **企业级热加载** | Enterprise Hot-Reload (5秒自动检测插件修改)
- **多人格AI系统** | Multi-Personality AI System
- **插件优先级系统** | Plugin Priority System

### 企业级模块 | Enterprise Modules (v2.0)
- **权限管理系统** | Permission System (Owner/Admin/Mod/VIP/User 多级权限)
- **请求限流器** | Rate Limiter (令牌桶算法 + 熔断器)
- **结构化日志** | Structured Logger (JSON格式 + TraceID关联)
- **Prometheus指标** | Metrics Exporter (Counter/Gauge/Histogram)
- **分布式追踪** | Trace System (Jaeger格式导出)
- **配置热重载** | Config Watcher (文件监控 + 回调通知)
- **插件沙箱** | Plugin Sandbox (权限隔离 + 资源限制)
- **AI响应缓存** | Response Cache (LRU + TTL + 持久化)
- **数据库层** | Database Layer (SQL语法 + 索引 + 事务)

### 插件系统 | Plugin System
- **Python 插件支持** | Python Plugin Support
- **C++ 原生插件** | C++ Native Plugins
- **AstrBot 插件兼容** | AstrBot Plugin Compatibility (支持加载AstrBot框架插件)
- **自动热加载** | Auto Hot-Reload
- **插件间通信** | Inter-Plugin Communication
- **插件沙箱隔离** | Plugin Sandbox Isolation

### 内置功能 | Built-in Features
- **AI 聊天** | AI Chat (多模型支持: Gemini/DeepSeek)
- **上下文记忆** | Context Memory (数据库存储 + 智能检索)
- **管理面板** | Admin Panel (HTTP API + 企业级监控)
- **节日日历** | Holiday Calendar (农历/公历节日)

---

## 快速开始 | Quick Start

### 环境要求 | Requirements
- Windows 10/11
- Python 3.8+ (自动检测)
- [LLOneBot](https://github.com/tlkppm/LLOneBot.git) 或其他 OneBot 11 实现

### 配置 | Configuration

编辑 `config.ini`:

```ini
[bot]
ws_url=ws://127.0.0.1:3001/

[ai]
api_url=YOUR_API_URL
api_key=YOUR_API_KEY
model=gpt-4

[admin]
port=8080
```

### 运行 | Run

```bash
LCHBOT.exe
```

---

## 插件开发 | Plugin Development

### Python 插件模板 | Python Plugin Template

```python
class MyPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "MyPlugin"
        self.version = "1.0.0"
        self.author = "YourName"
        self.description = "插件描述"
        self.priority = 50
    
    def on_load(self):
        print(f"[{self.name}] Plugin loaded")
    
    def on_message(self, event):
        msg = event.get("raw_message", "")
        user_id = event.get("user_id")
        group_id = event.get("group_id")
        
        if msg == "/hello":
            self.reply(event, "Hello World!")
            return True
        
        return False
    
    def reply(self, event, message):
        _lchbot_reply_queue.append({
            "action": "send_msg",
            "params": {
                "message_type": "group" if event.get("group_id") else "private",
                "user_id": event.get("user_id"),
                "group_id": event.get("group_id"),
                "message": message
            }
        })

_lchbot_plugins["MyPlugin"] = MyPlugin()
```

### 插件目录结构 | Plugin Directory Structure

```
plugins/
├── my_plugin.py          # 插件主文件
├── my_plugin_assets/     # 插件资源目录
│   ├── images/
│   └── data/
└── _helper.py            # 下划线开头的文件不会被加载
```

### 插件生命周期 | Plugin Lifecycle

1. **on_load()** - 插件加载时调用
2. **on_message(event)** - 收到消息时调用
3. **on_unload()** - 插件卸载时调用 (热加载时)

### 热加载说明 | Hot-Reload

- 框架每 **5秒** 检测一次插件文件修改
- 修改插件后 **无需重启** 框架
- 自动清理旧插件实例和模块缓存

---

## API 参考 | API Reference

### 消息事件 | Message Event

```python
event = {
    "post_type": "message",
    "message_type": "group",  # or "private"
    "user_id": 123456789,
    "group_id": 987654321,
    "raw_message": "消息内容",
    "sender": {
        "user_id": 123456789,
        "nickname": "用户昵称"
    }
}
```

### 发送消息 | Send Message

```python
_lchbot_reply_queue.append({
    "action": "send_msg",
    "params": {
        "message_type": "group",
        "group_id": 123456789,
        "message": "消息内容"
    }
})
```

### CQ码 | CQ Codes

```python
# 图片 (Base64)
f"[CQ:image,file=base64://{base64_data}]"

# @用户
f"[CQ:at,qq={user_id}]"

# 回复
f"[CQ:reply,id={message_id}]"
```

---

## 管理面板 | Admin Panel

访问 `http://127.0.0.1:8080` 进入管理面板

### API 端点 | API Endpoints

| 端点 | 方法 | 描述 |
|------|------|------|
| `/api/status` | GET | 获取状态 |
| `/api/plugins` | GET | 插件列表 |
| `/api/plugins/{name}/enable` | POST | 启用插件 |
| `/api/plugins/{name}/disable` | POST | 禁用插件 |
| `/api/reload` | POST | 重载系统 |
| `/api/metrics` | GET | 监控指标 (JSON) |
| `/api/permissions` | GET | 权限信息 |
| `/api/traces` | GET | 追踪记录 |
| `/api/cache` | GET/POST | 缓存状态/清理 |
| `/api/sandbox` | GET | 沙箱状态 |
| `/metrics` | GET | Prometheus格式指标 |

---

## 示例插件 | Example Plugins

### DeltaOps (三角洲行动)

完整的游戏插件示例，包含:
- 职业系统 (尖兵/医师/支援/侦察/工程/破障)
- 技能系统
- GIF动画生成
- 物品图标显示
- 小队系统

查看 `plugins/gunfight.py`

---

## 编译 | Build

### 要求 | Requirements
- Visual Studio 2019+ (推荐 2022)
- C++20 支持
- Windows SDK 10.0+

### 编译命令 | Build Command

```bash
MSBuild.exe LCHBOT.sln /p:Configuration=Release /p:Platform=x64
```

---

## 许可证 | License

MIT License

---

## 贡献 | Contributing

欢迎提交 Issue 和 Pull Request!

Welcome to submit Issues and Pull Requests!

---

<p align="center">
  Made with ❤️ by LCHBOT Team
</p>
