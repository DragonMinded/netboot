import os
import os.path
import threading

from typing import Dict, List, Optional, Sequence
from netboot.binary import Binary


class PatchException(Exception):
    pass


class PatchManager:
    def __init__(self, directories: Sequence[str]) -> None:
        self.__directories = list(directories)
        self.__lock: threading.Lock = threading.Lock()
        self.__cache: Dict[str, List[str]] = {}

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return [d for d in self.__directories]

    def patches(self, directory: str) -> List[str]:
        with self.__lock:
            if directory not in self.__directories:
                raise Exception(f"Directory {directory} is not managed by us!")
            return sorted([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])

    def recalculate(self, filename: Optional[str] = None) -> None:
        with self.__lock:
            if filename is None:
                self.__cache = {}
            else:
                if filename in self.__cache:
                    del self.__cache[filename]

    def patch_name(self, filename: str) -> str:
        with self.__lock:
            with open(filename, "r") as pp:
                patchlines = pp.readlines()

            return Binary.description(patchlines) or os.path.splitext(os.path.basename(filename))[0].replace('_', ' ')

    def patches_for_game(self, filename: str) -> List[str]:
        with self.__lock:
            # First, see if we already cached this file.
            if filename in self.__cache:
                return self.__cache[filename]

            with open(filename, "rb") as fp:
                # First, grab the file size, see if there are any patches at all for this file.
                length: int = os.fstat(fp.fileno()).st_size
                loaded: int = 0
                data: bytes = b""

                # Grab currently known patches
                patches: List[str] = []
                for directory in self.__directories:
                    patches.extend(os.path.join(directory, f) for f in os.listdir(directory))

                # Figure out which of these is valid for this filename
                valid_patches: List[str] = []
                for patch in patches:
                    try:
                        with open(patch, "r") as pp:
                            patchlines = pp.readlines()
                    except Exception:
                        continue
                    size = Binary.size(patchlines)
                    if size is None or size == length:
                        # Only read the file itself if there's one or more patches that we
                        # need to actually compare against. Also, only read the number of
                        # bytes needed to calculate if the patch matches.
                        needed_amount = Binary.needed_amount(patchlines)
                        if loaded < needed_amount:
                            rest = needed_amount - loaded
                            data = data + fp.read(rest)
                            loaded += rest

                        if Binary.can_patch(data, patchlines, ignore_size_differences=True)[0]:
                            valid_patches.append(patch)

            self.__cache[filename] = valid_patches
            return valid_patches
