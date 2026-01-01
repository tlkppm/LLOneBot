class AstrBotConfig(dict):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
    
    def get(self, key, default=None):
        return super().get(key, default)
