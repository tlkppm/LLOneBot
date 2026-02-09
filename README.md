# LCHBOT - QQ Bot Framework

<p align="center">
  <b>轻量级、高性能的QQ机器人框架</b><br>
  <b>Lightweight, High-Performance QQ Bot Framework</b>
</p>

---

## 简介 | Introduction

**LCHBOT** 是一个基于 OneBot 11 协议的现代化 QQ 机器人框架，采用纯 C++ 编写核心（零外部依赖），内置 SQLite 数据库、多模型 AI 对话系统和 Python 插件热加载，提供企业级的稳定性和扩展性。

**LCHBOT** is a modern QQ bot framework based on the OneBot 11 protocol. Written entirely in C++ (zero external dependencies) with built-in SQLite database, multi-model AI chat system, and Python plugin hot-reload support, it provides enterprise-level stability and extensibility.

## 特性 | Features

### 核心架构 | Core Architecture
- **纯 C++ 实现** | Pure C++ — 零第三方库依赖，内置 JSON 解析、HTTP/WebSocket 客户端、SQLite 引擎
- **OneBot 11 协议** | OneBot 11 Protocol — 完整实现消息收发、群管理、好友管理等 API
- **WebSocket 双向通信** | WebSocket — 正向/反向 WS 连接，自动重连与心跳
- **企业级热加载** | Enterprise Hot-Reload — 5 秒自动检测插件修改，无需重启

### AI 对话系统 | AI Chat System
- **多模型支持** | Multi-Model — Gemini / Claude / DeepSeek / Grok 等，可通过 `config/models.json` 自由配置
- **多人格系统** | Multi-Personality — 每个群可独立配置 AI 人格与提示词
- **智能工具调用** | Tool Use — AI 可在对话中自动调用以下工具：
  - `holiday` — 查询节日日期（农历/公历）
  - `keyword` / `sender` / `recent` / `date` — 搜索聊天记录
  - `users` / `summary` — 活跃用户排行与年度总结
  - `setcard` — 修改群成员名片（带群成员校验，防伪造 QQ 号）
  - `settitle` — 设置群成员专属头衔（仅群主可用）
- **群成员感知** | Member Awareness — 自动缓存群成员列表，AI 可精准识别昵称与 QQ 号对应关系
- **自动补偿机制** | Auto-Compensation — AI 未生成工具调用但文本中表达了操作意图时，自动提取并执行
- **上下文记忆** | Context Memory — SQLite 持久化存储，智能检索相关历史对话
- **AI 响应缓存** | Response Cache — LRU + TTL + 持久化，减少重复 API 调用

### 数据库 | Database
- **内置 SQLite** | Built-in SQLite — 编译进二进制，WAL 模式，支持索引与事务
- **自动迁移** | Auto Migration — 首次启动自动将旧文本格式数据库迁移至 SQLite，零数据丢失
- **聊天记录存储** | Message Storage — 按群/私聊分上下文存储，支持多维度查询

### 企业级模块 | Enterprise Modules
- **权限管理** | Permission System — Owner / Admin / Mod / VIP / User 多级权限
- **请求限流** | Rate Limiter — 令牌桶算法 + 熔断器
- **结构化日志** | Structured Logger — 多级别日志 + TraceID 关联
- **Prometheus 指标** | Metrics Exporter — Counter / Gauge / Histogram
- **分布式追踪** | Trace System — Jaeger 格式导出
- **配置热重载** | Config Watcher — 文件监控 + 回调通知
- **插件沙箱** | Plugin Sandbox — 权限隔离 + 资源限制

### 插件系统 | Plugin System
- **Python 插件** | Python Plugin — 完整生命周期（load / message / unload）
- **C++ 原生插件** | C++ Native Plugin — 高性能原生扩展
- **Pipeline 调度** | Pipeline Scheduler — 异步任务队列，支持长耗时插件
- **自动热加载** | Auto Hot-Reload — 5 秒检测，自动清理旧实例
- **群成员缓存共享** | Member Cache Sharing — Python 插件可访问群成员数据

---

## 快速开始 | Quick Start

### 环境要求 | Requirements
- Windows 10/11
- Visual Studio 2019+（编译）
- Python 3.8+（插件运行，自动检测）
- OneBot 11 实现（如 [Lagrange](https://github.com/LagrangeDev/Lagrange.Core)、NapCat 等）

### 配置 | Configuration

**`config.ini`** — 基础配置：

```ini
[bot]
ws_url=ws://127.0.0.1:3001/

[admin]
port=8080
```

**`config/models.json`** — AI 模型配置：

```json
{
  "models": {
    "gemini": {
      "api_url": "https://generativelanguage.googleapis.com/v1/...",
      "api_key": "YOUR_KEY",
      "model": "gemini-2.0-flash",
      "request_format": "gemini"
    },
    "claude": {
      "api_url": "https://api.anthropic.com/v1/messages",
      "api_key": "YOUR_KEY",
      "model": "claude-sonnet-4-20250514",
      "request_format": "claude"
    }
  },
  "default_model": "gemini"
}
```

**`config/personalities.json`** — AI 人格配置（每群可独立设置）

### 编译 & 运行 | Build & Run

```bash
# 编译
MSBuild.exe LCHBOT.vcxproj /p:Configuration=Release /p:Platform=x64

# 运行
x64\Release\LCHBOT.exe
```

---

## 插件开发 | Plugin Development

### Python 插件模板

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
        group_id = event.get("group_id")
        
        if msg == "/hello":
            self.reply(event, "Hello World!")
            return True
        return False
    
    def reply(self, event, message):
        _lchbot_reply_queue.append({
            "action": "send_group_msg" if event.get("group_id") else "send_private_msg",
            "group_id": event.get("group_id"),
            "user_id": event.get("user_id"),
            "message": message
        })

_lchbot_plugins["MyPlugin"] = MyPlugin()
```

### 目录结构

```
plugins/
├── my_plugin.py          # 插件主文件（自动加载）
├── my_plugin_assets/     # 插件资源目录
└── _helper.py            # 下划线开头不加载
```

### 生命周期

| 方法 | 触发时机 |
|------|----------|
| `on_load()` | 插件加载 / 热重载 |
| `on_message(event)` | 收到群聊或私聊消息 |
| `on_unload()` | 插件卸载（热重载前） |

---

## AI 工具系统 | AI Tool System

AI 在 `[THINK]` 块中通过 `[QUERY:type=arg]` 格式调用工具，系统自动执行并将结果反馈给 AI 进行下一轮推理。

| 工具 | 格式 | 说明 |
|------|------|------|
| `holiday` | `[QUERY:holiday=春节]` | 查询节日日期 |
| `keyword` | `[QUERY:keyword=关键词]` | 搜索聊天记录 |
| `sender` | `[QUERY:sender=用户名]` | 搜索特定用户发言 |
| `recent` | `[QUERY:recent=20]` | 获取最近 N 条记录 |
| `users` | `[QUERY:users]` | 活跃用户排行 |
| `summary` | `[QUERY:summary=2025]` | 年度总结 |
| `date` | `[QUERY:date=2025-12-25]` | 查询指定日期记录 |
| `setcard` | `[QUERY:setcard=QQ号,新名片]` | 修改群名片 |
| `settitle` | `[QUERY:settitle=QQ号,头衔]` | 设置专属头衔 |

### 安全机制

- **群成员校验** — `setcard` / `settitle` 执行前验证目标 QQ 是否为本群成员
- **成员列表注入** — 群成员列表自动注入 AI 上下文，防止 AI 编造 QQ 号
- **占位符过滤** — 自动识别并拒绝 AI 生成的模板占位参数
- **工具白名单** — 仅允许已注册的工具类型执行

---

## 管理面板 | Admin Panel

访问 `http://127.0.0.1:8080` 进入 Web 管理面板。

| 端点 | 方法 | 描述 |
|------|------|------|
| `/api/status` | GET | 系统状态 |
| `/api/plugins` | GET | 插件列表 |
| `/api/plugins/{name}/enable` | POST | 启用插件 |
| `/api/plugins/{name}/disable` | POST | 禁用插件 |
| `/api/reload` | POST | 重载系统 |
| `/api/metrics` | GET | 监控指标 |
| `/api/cache` | GET/POST | 缓存管理 |
| `/metrics` | GET | Prometheus 格式指标 |

---

## 项目结构 | Project Structure

```
LCHBOT/
├── src/
│   ├── ai/                  # AI 对话系统
│   │   ├── AIService.h      # AI 服务核心（多模型、工具调用、自动补偿）
│   │   ├── ContextDatabase.h # 上下文数据库（SQLite 持久化）
│   │   └── PersonalitySystem.h # 多人格管理
│   ├── api/
│   │   └── OneBotApi.h      # OneBot 11 API 封装
│   ├── bot/
│   │   └── Bot.h            # 机器人主逻辑 + 群成员缓存
│   ├── core/
│   │   ├── Database.h       # SQLite 数据库引擎 + 自动迁移
│   │   ├── GroupMemberCache.h # 群成员缓存（线程安全）
│   │   ├── Calendar.h       # 节日日历（农历/公历）
│   │   ├── Logger.h         # 结构化日志
│   │   └── ErrorCodes.h     # 错误码体系
│   ├── plugin/
│   │   ├── AIPlugin.h       # AI 聊天插件
│   │   ├── PluginManager.h  # 插件管理器
│   │   └── PythonPlugin.h   # Python 插件引擎 + Pipeline
│   └── main.cpp
├── config/
│   ├── models.json          # AI 模型配置
│   ├── personalities.json   # 人格配置
│   └── holidays.json        # 节日数据
├── plugins/                 # Python 插件目录
├── admin/
│   └── index.html           # 管理面板前端
└── LCHBOT.vcxproj           # VS 工程文件
```

---

## 许可证 | License

MIT License

---

<p align="center">
  Made with ❤️ by LCHBOT Team
</p>
