import os
import sys
import asyncio
import importlib.util
import yaml
import base64
import traceback
import threading

try:
    plugins_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    plugins_dir = os.path.join(os.getcwd(), "plugins")
if plugins_dir not in sys.path:
    sys.path.insert(0, plugins_dir)

from astrbot.api.event import AstrMessageEvent
from astrbot.api.star import Context
from astrbot.api import AstrBotConfig

class AstrBotPluginLoader(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "AstrBotLoader"
        self.version = "3.0.0"
        self.author = "LCHBOT"
        self.description = "AstrBot插件加载器"
        self.icon = "🔌"
        self.priority = 10
        
        self.loaded_plugins = {}
        self.command_handlers = {}
        self.plugin_instances = {}
        self.plugins_dir = plugins_dir
    
    def on_load(self):
        self._scan_astrbot_plugins()
        self._update_plugin_info()
    
    def _update_plugin_info(self):
        if self.loaded_plugins:
            names = [p["metadata"].get("name", k) for k, p in self.loaded_plugins.items()]
            self.description = f"AstrBot插件加载器 - 已加载: {', '.join(names)}"
    
    def _scan_astrbot_plugins(self):
        
        for item in os.listdir(plugins_dir):
            item_path = os.path.join(plugins_dir, item)
            if not os.path.isdir(item_path):
                continue
            
            main_py = os.path.join(item_path, "main.py")
            metadata_yaml = os.path.join(item_path, "metadata.yaml")
            
            if os.path.exists(main_py) and os.path.exists(metadata_yaml):
                try:
                    self._load_astrbot_plugin(item, item_path)
                except Exception as e:
                    print(f"[AstrBotLoader] Failed to load plugin {item}: {e}")
                    traceback.print_exc()
    
    def _load_astrbot_plugin(self, plugin_name, plugin_path):
        with open(os.path.join(plugin_path, "metadata.yaml"), "r", encoding="utf-8") as f:
            metadata = yaml.safe_load(f)
        
        parent_dir = os.path.dirname(plugin_path)
        if parent_dir not in sys.path:
            sys.path.insert(0, parent_dir)
        if plugin_path not in sys.path:
            sys.path.insert(0, plugin_path)
        
        init_file = os.path.join(plugin_path, "__init__.py")
        if not os.path.exists(init_file):
            with open(init_file, "w", encoding="utf-8") as f:
                f.write("")
        
        old_cwd = os.getcwd()
        os.chdir(plugin_path)
        try:
            module = importlib.import_module(f"{plugin_name}.main")
        finally:
            os.chdir(old_cwd)
        
        plugin_class = None
        for attr_name in dir(module):
            attr = getattr(module, attr_name)
            if isinstance(attr, type) and hasattr(attr, '_astrbot_register'):
                plugin_class = attr
                break
        
        if not plugin_class:
            for attr_name in dir(module):
                attr = getattr(module, attr_name)
                if isinstance(attr, type) and attr_name not in ['Star', 'Context', 'StarTools']:
                    if hasattr(attr, 'info') or hasattr(attr, '__init__'):
                        plugin_class = attr
                        break
        
        if plugin_class:
            context = Context()
            config = AstrBotConfig()
            
            import threading
            instance = None
            init_error = None
            
            def run_in_thread():
                nonlocal instance, init_error
                loop = asyncio.new_event_loop()
                asyncio.set_event_loop(loop)
                try:
                    async def init_and_wait():
                        nonlocal instance
                        try:
                            instance = plugin_class(context, config)
                        except TypeError:
                            try:
                                instance = plugin_class(context)
                            except TypeError:
                                instance = plugin_class()
                        await asyncio.sleep(2)
                        pending = asyncio.all_tasks(loop)
                        if pending:
                            await asyncio.gather(*pending, return_exceptions=True)
                    loop.run_until_complete(init_and_wait())
                except Exception as e:
                    init_error = e
                finally:
                    try:
                        pending = asyncio.all_tasks(loop)
                        for task in pending:
                            task.cancel()
                        loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
                    except:
                        pass
                    loop.close()
            
            thread = threading.Thread(target=run_in_thread)
            thread.start()
            thread.join(timeout=10)
            
            if init_error:
                raise init_error
            if instance is None:
                raise RuntimeError("Plugin initialization timeout")
            
            self.plugin_instances[plugin_name] = instance
            
            for method_name in dir(instance):
                method = getattr(instance, method_name)
                if callable(method) and hasattr(method, '_astrbot_command'):
                    cmd = method._astrbot_command
                    self.command_handlers[cmd] = (instance, method)
                    print(f"[AstrBotLoader] Registered command: {cmd}")
            
            self.loaded_plugins[plugin_name] = {
                "metadata": metadata,
                "instance": instance,
                "path": plugin_path
            }
            print(f"[AstrBotLoader] Loaded AstrBot plugin: {metadata.get('name', plugin_name)} v{metadata.get('version', '?')}")
    
    def _clean_message(self, msg):
        import re
        msg = re.sub(r'\[CQ:[^\]]+\]', '', msg)
        msg = msg.strip()
        if msg.startswith('/'):
            msg = msg[1:]
        return msg.strip()
    
    def _process_result(self, event, result):
        try:
            if hasattr(result, 'chain') and result.chain:
                parts = []
                for comp in result.chain:
                    parts.append(str(comp))
                msg = "".join(parts)
                if msg:
                    self.reply(event, msg)
            elif hasattr(result, 'result_type'):
                if result.result_type == "text":
                    self.reply(event, result.content)
                elif result.result_type == "image":
                    if os.path.exists(result.content):
                        with open(result.content, "rb") as f:
                            img_data = base64.b64encode(f.read()).decode()
                        self.reply(event, f"[CQ:image,file=base64://{img_data}]")
                elif result.result_type == "chain":
                    self.reply(event, result.content)
        except Exception as e:
            print(f"[AstrBotLoader] Result processing error: {e}")
            traceback.print_exc()
    
    def _background_handle_command(self, event, cmd, handler):
        def run_in_thread():
            try:
                astr_event = AstrMessageEvent(event)
                astr_event.message_str = self._clean_message(event.get("raw_message", ""))
                
                loop = asyncio.new_event_loop()
                asyncio.set_event_loop(loop)
                
                async def run_handler_streaming():
                    try:
                        async for result in handler(astr_event):
                            if result:
                                self._process_result(event, result)
                    except Exception as e:
                        print(f"[AstrBotLoader] Handler error: {e}")
                        traceback.print_exc()
                
                loop.run_until_complete(run_handler_streaming())
                loop.close()
                    
            except Exception as e:
                print(f"[AstrBotLoader] Background error for {cmd}: {e}")
                traceback.print_exc()
        
        t = threading.Thread(target=run_in_thread, daemon=True)
        t.start()
    
    def on_message(self, event):
        raw_msg = event.get("raw_message", "").strip()
        msg = self._clean_message(raw_msg)
        
        for cmd, (instance, handler) in self.command_handlers.items():
            if msg == cmd or msg.startswith(cmd + " "):
                self._background_handle_command(event.copy() if hasattr(event, 'copy') else dict(event), cmd, handler)
                return True
        
        return False

plugin = AstrBotPluginLoader()
register_plugin(plugin)
