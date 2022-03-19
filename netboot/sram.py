import os
import os.path
import threading

from typing import Dict, List, Optional, Sequence
from arcadeutils import FileBytes
from naomi import NaomiRom, NaomiSettingsPatcher


class SRAMException(Exception):
    pass


class SRAMManager:
    def __init__(self, directories: Sequence[str]) -> None:
        self.__directories = list(directories)
        self.__lock: threading.Lock = threading.Lock()
        self.__cache: Dict[str, List[str]] = {}

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return [d for d in self.__directories]

    def srams(self, directory: str) -> List[str]:
        with self.__lock:
            if directory not in self.__directories:
                raise SRAMException(f"Directory {directory} is not managed by us!")
            return sorted([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])

    def recalculate(self, filename: Optional[str] = None) -> None:
        with self.__lock:
            if filename is None:
                self.__cache = {}
            else:
                if filename in self.__cache:
                    del self.__cache[filename]

    def sram_name(self, filename: str) -> str:
        with self.__lock:
            return os.path.splitext(os.path.basename(filename))[0].replace('_', ' ')

    def srams_for_game(self, filename: str) -> List[str]:
        with self.__lock:
            # First, see if we already cached this file.
            if filename in self.__cache:
                return self.__cache[filename]

            valid_srams: List[str] = []
            with open(filename, "rb") as fp:
                # First, grab the file size, see if there are any srams at all for this file.
                data = FileBytes(fp)

                # If it's a Naomi ROM, so SRAMs must be 32kb in size.
                rom = NaomiRom(data)
                if rom.valid:
                    # Grab currently known SRAMs
                    srams: List[str] = []
                    for directory in self.__directories:
                        srams.extend(os.path.join(directory, f) for f in os.listdir(directory))

                    # Figure out which of these is valid for this ROM type.
                    for sram in srams:
                        try:
                            size = os.path.getsize(sram)
                        except Exception:
                            size = 0

                        if size == NaomiSettingsPatcher.SRAM_SIZE:
                            valid_srams.append(sram)

            self.__cache[filename] = valid_srams
            return valid_srams
