import os
import base64 as b64_module

class BaseMessageComponent:
    type = "Unknown"
    def toDict(self):
        return {"type": self.type.lower(), "data": {}}

class Plain(BaseMessageComponent):
    type = "Plain"
    def __init__(self, text="", **kwargs):
        self.text = text
    
    def __str__(self):
        return self.text

class At(BaseMessageComponent):
    type = "At"
    def __init__(self, qq=None, name=None, **kwargs):
        self.qq = qq
        self.name = name or ""
    
    def __str__(self):
        return f"[CQ:at,qq={self.qq}]"

class AtAll(At):
    def __init__(self, **kwargs):
        super().__init__(qq="all", **kwargs)

class Image(BaseMessageComponent):
    type = "Image"
    def __init__(self, file=None, path=None, url=None, **kwargs):
        self.file = file
        self.path = path
        self.url = url
    
    @staticmethod
    def fromFileSystem(path, **kwargs):
        return Image(file=f"file:///{os.path.abspath(path)}", path=path, **kwargs)
    
    @staticmethod
    def fromURL(url, **kwargs):
        return Image(file=url, url=url, **kwargs)
    
    @staticmethod
    def fromBase64(base64_str, **kwargs):
        return Image(file=f"base64://{base64_str}", **kwargs)
    
    @staticmethod
    def fromBytes(byte_data):
        return Image.fromBase64(b64_module.b64encode(byte_data).decode())
    
    def __str__(self):
        if self.path:
            return f"[CQ:image,file=file:///{os.path.abspath(self.path)}]"
        if self.url:
            return f"[CQ:image,file={self.url}]"
        if self.file:
            if self.file.startswith("base64://"):
                return f"[CQ:image,file={self.file}]"
            return f"[CQ:image,file={self.file}]"
        return ""

class Face(BaseMessageComponent):
    type = "Face"
    def __init__(self, id=0, **kwargs):
        self.id = id
    
    def __str__(self):
        return f"[CQ:face,id={self.id}]"

class Record(BaseMessageComponent):
    type = "Record"
    def __init__(self, file=None, path=None, url=None, **kwargs):
        self.file = file
        self.path = path
        self.url = url
    
    @staticmethod
    def fromFileSystem(path, **kwargs):
        return Record(file=f"file:///{os.path.abspath(path)}", path=path, **kwargs)
    
    @staticmethod
    def fromURL(url, **kwargs):
        return Record(file=url, url=url, **kwargs)
    
    @staticmethod
    def fromBase64(base64_str, **kwargs):
        return Record(file=f"base64://{base64_str}", **kwargs)
    
    def __str__(self):
        if self.path:
            return f"[CQ:record,file=file:///{os.path.abspath(self.path)}]"
        if self.url:
            return f"[CQ:record,file={self.url}]"
        if self.file:
            return f"[CQ:record,file={self.file}]"
        return ""

class Reply(BaseMessageComponent):
    type = "Reply"
    def __init__(self, id=None, **kwargs):
        self.id = id
    
    def __str__(self):
        return f"[CQ:reply,id={self.id}]"

class Forward(BaseMessageComponent):
    type = "Forward"
    def __init__(self, id=None, **kwargs):
        self.id = id

class Node(BaseMessageComponent):
    type = "Node"
    def __init__(self, content=None, name="", uin="0", **kwargs):
        self.content = content or []
        self.name = name
        self.uin = uin

class Nodes(BaseMessageComponent):
    type = "Nodes"
    def __init__(self, nodes=None, **kwargs):
        self.nodes = nodes or []

class Video(BaseMessageComponent):
    type = "Video"
    def __init__(self, file=None, **kwargs):
        self.file = file
    
    @staticmethod
    def fromFileSystem(path, **kwargs):
        return Video(file=f"file:///{os.path.abspath(path)}", **kwargs)
    
    @staticmethod
    def fromURL(url, **kwargs):
        return Video(file=url, **kwargs)

class File(BaseMessageComponent):
    type = "File"
    def __init__(self, name="", file="", url="", **kwargs):
        self.name = name
        self.file_ = file
        self.url = url
