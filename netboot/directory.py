import os
import os.path
import threading
import zlib

from typing import Dict, List, Mapping, Sequence
from netboot.cabinet import CabinetRegionEnum
from naomi import NaomiRom, NaomiRomRegionEnum


class DirectoryException(Exception):
    pass


class DirectoryManager:
    def __init__(self, directories: Sequence[str], checksums: Mapping[str, str]) -> None:
        self.__checksums: Dict[str, str] = dict(checksums)
        self.__directories = list(directories)
        self.__names: Dict[str, str] = {}
        self.__lock: threading.Lock = threading.Lock()

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return [d for d in self.__directories]

    @property
    def checksums(self) -> Dict[str, str]:
        with self.__lock:
            return self.__checksums

    def games(self, directory: str) -> List[str]:
        with self.__lock:
            if directory not in self.__directories:
                raise Exception(f"Directory {directory} is not managed by us!")
            return sorted([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])

    def game_name(self, filename: str, region: CabinetRegionEnum) -> str:
        with self.__lock:
            local_key = f"{region.value}-{filename}"
            if local_key in self.__names:
                return self.__names[local_key]

            # Grab enough of the header for a match
            with open(filename, "rb") as fp:
                data = fp.read(0x1000)
                length = os.fstat(fp.fileno()).st_size

            # Now, check and see if we have a checksum match
            crc = zlib.crc32(data, 0)
            checksum = f"{region.value}-{crc}-{length}"
            if checksum in self.__checksums:
                self.__names[local_key] = self.__checksums[checksum]
                return self.__names[local_key]

            # Now, see if we can figure out from the header
            rom = NaomiRom(data)
            if rom.valid:
                # Arbitrarily choose USA region as default
                naomi_region = {
                    CabinetRegionEnum.REGION_JAPAN: NaomiRomRegionEnum.REGION_JAPAN,
                    CabinetRegionEnum.REGION_USA: NaomiRomRegionEnum.REGION_USA,
                    CabinetRegionEnum.REGION_EXPORT: NaomiRomRegionEnum.REGION_EXPORT,
                    CabinetRegionEnum.REGION_KOREA: NaomiRomRegionEnum.REGION_KOREA,
                    CabinetRegionEnum.REGION_AUSTRALIA: NaomiRomRegionEnum.REGION_AUSTRALIA,
                }.get(region, NaomiRomRegionEnum.REGION_JAPAN)
                self.__names[local_key] = rom.names[naomi_region]
                self.__checksums[checksum] = self.__names[local_key]
                return self.__names[local_key]

            # Finally, fall back to filename, getting rid of extensions and underscores
            self.__names[local_key] = os.path.splitext(os.path.basename(filename))[0].replace('_', ' ')
            self.__checksums[checksum] = self.__names[local_key]
            return self.__names[local_key]

    def rename_game(self, filename: str, region: CabinetRegionEnum, name: str) -> None:
        with self.__lock:
            # Make the local key
            local_key = f"{region.value}-{filename}"

            # Grab enough of the header for a match
            with open(filename, "rb") as fp:
                data = fp.read(0x1000)
                length = os.fstat(fp.fileno()).st_size

            # Now, check and see if we have a checksum match
            crc = zlib.crc32(data, 0)
            checksum = f"{region.value}-{crc}-{length}"

            # Update the value
            self.__names[local_key] = name
            self.__checksums[checksum] = self.__names[local_key]
