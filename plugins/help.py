class HelpPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "HelpPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "Help command plugin"
        self.priority = 100
        
        self.commands = {
            "help": "Show this help message",
            "status": "Show bot status",
            "plugins": "List all plugins"
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
        help_text = "=== LCHBOT Help ===\n"
        help_text += "Available commands:\n"
        for cmd, desc in self.commands.items():
            help_text += f"  /{cmd} - {desc}\n"
        self.reply(event, help_text)
    
    def show_status(self, event):
        status_text = "=== LCHBOT Status ===\n"
        status_text += "Status: Running\n"
        status_text += "Version: 1.0.0\n"
        status_text += "Protocol: OneBot 11"
        self.reply(event, status_text)
    
    def show_plugins(self, event):
        plugins_text = "=== Loaded Plugins ===\n"
        plugins_text += "- HelpPlugin v1.0.0\n"
        plugins_text += "- ExamplePlugin v1.0.0"
        self.reply(event, plugins_text)

register_plugin(HelpPlugin())
