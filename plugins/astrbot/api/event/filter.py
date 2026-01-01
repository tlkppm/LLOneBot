import functools

def command(cmd_name):
    def decorator(func):
        func._astrbot_command = cmd_name
        @functools.wraps(func)
        async def wrapper(*args, **kwargs):
            async for result in func(*args, **kwargs):
                yield result
        wrapper._astrbot_command = cmd_name
        return wrapper
    return decorator
