class Context:
    def __init__(self):
        self.config = {}
    
    def get_config(self):
        return self.config

class Star:
    def __init__(self, context: Context = None):
        self.context = context or Context()
    
    @classmethod
    def info(cls):
        return {
            "name": "unknown",
            "version": "1.0.0",
            "description": "",
            "author": "unknown"
        }

class StarTools:
    pass

def register(name, short_name, desc, version):
    def decorator(cls):
        cls._astrbot_register = {
            "name": name,
            "short_name": short_name,
            "desc": desc,
            "version": version
        }
        return cls
    return decorator
