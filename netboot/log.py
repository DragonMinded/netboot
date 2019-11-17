import os
import sys
import threading


lock: threading.Lock = threading.Lock()


def log(msg: str, *, newline: bool = True) -> None:
    with lock:
        print(msg, file=sys.stderr, end=os.linesep if newline else "")
