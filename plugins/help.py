class HelpPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "HelpPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "命令帮助"
        self.priority = 100
        
        self.commands = {
            "help": "显示命令帮助",
            "status": "查看机器人运行状态",
            "plugins": "查看当前已加载插件",
            "天气 城市": "查询城市实时天气卡片",
            "60秒早报": "发送每日早报图片",
            "今日运势": "生成今日运势卡片",
            "骰子 [面数]": "掷一个指定面数的骰子",
            "抽签": "抽取今日灵签",
            "选择 选项1/选项2": "在多个选项中随机选择",
            "真心话": "获取一个真心话题目",
            "大冒险": "获取一个大冒险任务"
        }
    
    def on_load(self):
        print(f"[{self.name}] Plugin loaded")
    
    def on_message(self, event):
        raw_message = event.get("raw_message", "").strip()
        
        import re
        cmd = re.sub(r'\[CQ:[^\]]+\]', '', raw_message).strip().lower()
        
        if cmd == "help" or cmd == "/help":
            self.show_help(event)
            return True
        
        if cmd == "status" or cmd == "/status":
            self.show_status(event)
            return True
        
        if cmd == "plugins" or cmd == "/plugins":
            self.show_plugins(event)
            return True
        
        return False
    
    def show_help(self, event):
        help_text = "=== LCHBOT 命令帮助 ===\n"
        for cmd, desc in self.commands.items():
            help_text += f"  /{cmd} - {desc}\n"
        self.reply(event, help_text)
    
    def show_status(self, event):
        status_text = "=== LCHBOT 状态 ===\n"
        status_text += "状态：运行中\n"
        status_text += "版本：1.0.0\n"
        status_text += "协议：OneBot 11"
        self.reply(event, status_text)
    
    def show_plugins(self, event):
        plugins = globals().get("_lchbot_plugins", {})
        if isinstance(plugins, dict) and plugins:
            lines = ["=== 已加载插件 ==="]
            for name in sorted(plugins.keys()):
                plugin = plugins[name]
                version = getattr(plugin, "version", "?")
                description = getattr(plugin, "description", "")
                line = f"- {name} v{version}"
                if description:
                    line += f" | {description}"
                lines.append(line)
            plugins_text = "\n".join(lines)
        else:
            plugins_text = "=== 已加载插件 ===\n当前无法枚举插件列表"
        self.reply(event, plugins_text)

register_plugin(HelpPlugin())
