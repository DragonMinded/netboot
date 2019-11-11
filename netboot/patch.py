import os
import os.path
import threading

from typing import List, Sequence
from netboot.binary import Binary


class PatchException(Exception):
    pass


class PatchManager:
    def __init__(self, directories: Sequence[str]) -> None:
        self.__directories = list(directories)
        self.__lock: threading.Lock = threading.Lock()

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return [d for d in self.__directories]

    def patches(self, directory: str) -> List[str]:
        with self.__lock:
            if directory not in self.__directories:
                raise Exception(f"Directory {directory} is not managed by us!")
            return sorted([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])

    def patches_for_game(self, filename: str) -> List[str]:
        with self.__lock:
            # Grab game to see if any patches are applicable
            with open(filename, "rb") as fp:
                data = fp.read()

            # Grab currently known patches
            patches: List[str] = []
            for directory in self.__directories:
                patches.extend(f for f in os.listdir(directory))

            # Figure out which of these is valid for this filename
            valid_patches: List[str] = []
            for patch in patches:
                with open(patch, "r") as pp:
                    patchlines = pp.readlines()

                if Binary.can_patch(data, patchlines):
                    valid_patches.append(patch)

            return valid_patches
