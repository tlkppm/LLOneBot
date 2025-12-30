class ExamplePlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "ExamplePlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "Example Python Plugin"
    
    def on_load(self):
        print(f"[{self.name}] Plugin loaded")
    
    def on_unload(self):
        print(f"[{self.name}] Plugin unloaded")
    
    def on_message(self, event):
        raw_message = event.get("raw_message", "")
        user_id = event.get("user_id", 0)
        
        if raw_message == "/ping":
            print(f"[{self.name}] Received ping from {user_id}")
            return True
        
        return False
    
    def on_group_message(self, event):
        raw_message = event.get("raw_message", "")
        group_id = event.get("group_id", 0)
        
        if raw_message.startswith("/echo "):
            text = raw_message[6:]
            print(f"[{self.name}] Echo in group {group_id}: {text}")
            return True
        
        return False

register_plugin(ExamplePlugin())
