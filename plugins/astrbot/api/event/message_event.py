import os
from enum import Enum, auto

class EventResultType(Enum):
    CONTINUE = auto()
    STOP = auto()

class ResultContentType(Enum):
    LLM_RESULT = auto()
    GENERAL_RESULT = auto()
    STREAMING_RESULT = auto()

class MessageChain:
    def __init__(self, chain=None):
        self.chain = chain or []
        self.use_t2i_ = None
    
    def message(self, text):
        from ..message_components import Plain
        self.chain.append(Plain(text))
        return self
    
    def url_image(self, url):
        from ..message_components import Image
        self.chain.append(Image.fromURL(url))
        return self
    
    def file_image(self, path):
        from ..message_components import Image
        self.chain.append(Image.fromFileSystem(path))
        return self
    
    def base64_image(self, b64):
        from ..message_components import Image
        self.chain.append(Image.fromBase64(b64))
        return self
    
    def at(self, name, qq):
        from ..message_components import At
        self.chain.append(At(qq=qq, name=name))
        return self
    
    def get_plain_text(self):
        from ..message_components import Plain
        return " ".join([c.text for c in self.chain if isinstance(c, Plain)])

class MessageEventResult(MessageChain):
    def __init__(self, chain=None):
        super().__init__(chain)
        self.result_type = EventResultType.CONTINUE
        self.result_content_type = ResultContentType.GENERAL_RESULT
    
    def stop_event(self):
        self.result_type = EventResultType.STOP
        return self
    
    def continue_event(self):
        self.result_type = EventResultType.CONTINUE
        return self
    
    def is_stopped(self):
        return self.result_type == EventResultType.STOP

CommandResult = MessageEventResult

class MessageResult:
    def __init__(self, result_type, content, event):
        self.result_type = result_type
        self.content = content
        self.event = event

class AstrMessageEvent:
    def __init__(self, raw_event=None):
        self._raw_event = raw_event or {}
        self._replies = []
        self._result = None
        self.message_str = self._raw_event.get("raw_message", "")
        self.session_id = self._get_session_id()
        self.unified_msg_origin = self.session_id
        self.role = "member"
        self.is_wake = True
        self.is_at_or_wake_command = True
        self.call_llm = False
        if self._raw_event.get("sender", {}).get("role") in ["owner", "admin"]:
            self.role = "admin"
    
    def _get_session_id(self):
        if self._raw_event.get("message_type") == "group":
            return f"aiocqhttp:group:{self._raw_event.get('group_id', 0)}"
        return f"aiocqhttp:private:{self._raw_event.get('user_id', 0)}"
    
    def get_sender_id(self):
        return str(self._raw_event.get("user_id", 0))
    
    def get_sender_name(self):
        sender = self._raw_event.get("sender", {})
        return sender.get("card") or sender.get("nickname", "")
    
    def get_group_id(self):
        return str(self._raw_event.get("group_id", 0))
    
    def get_self_id(self):
        return str(self._raw_event.get("self_id", 0))
    
    def get_message_str(self):
        return self.message_str
    
    def is_private_chat(self):
        return self._raw_event.get("message_type") != "group"
    
    def is_admin(self):
        return self.role == "admin"
    
    def set_result(self, result):
        if isinstance(result, str):
            result = MessageEventResult().message(result)
        self._result = result
    
    def get_result(self):
        return self._result
    
    def stop_event(self):
        if self._result is None:
            self.set_result(MessageEventResult().stop_event())
        else:
            self._result.stop_event()
    
    def make_result(self):
        return MessageEventResult()
    
    def plain_result(self, text):
        result = MessageResult("text", text, self)
        self._replies.append(result)
        return result
    
    def image_result(self, url_or_path):
        if url_or_path.startswith("http"):
            result = MessageResult("url_image", url_or_path, self)
        else:
            result = MessageResult("image", url_or_path, self)
        self._replies.append(result)
        return result
    
    def chain_result(self, chain):
        parts = []
        for item in chain:
            parts.append(str(item))
        result = MessageResult("chain", "".join(parts), self)
        self._replies.append(result)
        return result
    
    def result_message(self, chain):
        return self.chain_result(chain)
    
    def get_replies(self):
        replies = self._replies.copy()
        self._replies.clear()
        return replies
