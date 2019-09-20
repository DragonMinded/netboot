import threading

from typing import List, Sequence


class DirectoryException(Exception):
    pass


class DirectoryManager:
    def __init__(self, directories: Sequence[str]) -> None:
        self.__directories = list(directories)
        self.__lock: threading.Lock = threading.Lock()

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return [d for d in self.__directories]
