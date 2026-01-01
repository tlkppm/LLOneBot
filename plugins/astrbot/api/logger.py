import logging
import sys

class AstrBotLogger:
    def __init__(self):
        self._logger = logging.getLogger("AstrBot")
        self._logger.setLevel(logging.DEBUG)
        if not self._logger.handlers:
            handler = logging.StreamHandler(sys.stdout)
            handler.setFormatter(logging.Formatter('[%(levelname)s] [AstrBot] %(message)s'))
            self._logger.addHandler(handler)
    
    def info(self, msg):
        self._logger.info(msg)
    
    def debug(self, msg):
        self._logger.debug(msg)
    
    def warning(self, msg):
        self._logger.warning(msg)
    
    def error(self, msg):
        self._logger.error(msg)
    
    def critical(self, msg):
        self._logger.critical(msg)

logger = AstrBotLogger()
