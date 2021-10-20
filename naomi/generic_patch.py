#! /usr/bin/env python3
from typing import Union, overload

from arcadeutils import FileBytes
from naomi.rom import NaomiRom


@overload
def _patch_rom(data: bytes, search: bytes, replace: bytes) -> bytes:
    ...


@overload
def _patch_rom(data: FileBytes, search: bytes, replace: bytes) -> FileBytes:
    ...


def _patch_rom(data: Union[bytes, FileBytes], search: bytes, replace: bytes) -> Union[bytes, FileBytes]:
    rom = NaomiRom(data)
    if not rom.valid:
        raise Exception("ROM file does not appear to be a Naomi netboot ROM!")

    # Now, find the location of our magic string in the main executable.
    exec_data = rom.main_executable
    patch_location = None
    search_len = len(search)

    # Look through all copied sections.
    for section in exec_data.sections:
        if isinstance(data, bytes):
            for rloc in range(section.length - (search_len - 1)):
                loc = rloc + section.offset
                if data[loc:(loc + search_len)] == search:
                    patch_location = loc
                    break
            if patch_location is not None:
                break
        elif isinstance(data, FileBytes):
            patch_location = data.search(search, start=section.offset, end=section.offset + section.length)
            if patch_location is not None:
                break

    if patch_location is None:
        raise Exception("ROM file does not have a suitable spot for the patch!")

    # Now, generate a patch with this updated data overlaid on the original rom
    if isinstance(data, bytes):
        return data[:patch_location] + replace + data[(patch_location + len(replace)):]
    elif isinstance(data, FileBytes):
        data = data.clone()
        data[patch_location:(patch_location + len(replace))] = replace
        return data
    else:
        raise Exception("Logic error!")


@overload
def force_freeplay(data: bytes) -> bytes:
    ...


@overload
def force_freeplay(data: FileBytes) -> FileBytes:
    ...


def force_freeplay(data: Union[bytes, FileBytes]) -> Union[bytes, FileBytes]:
    return _patch_rom(data, bytes([0x42, 0x84, 0xEC, 0x31, 0x0C, 0x60, 0x04, 0x1E, 0x43, 0x84]), bytes([0x1A, 0xE0]))


@overload
def force_no_attract_sound(data: bytes) -> FileBytes:
    ...


@overload
def force_no_attract_sound(data: FileBytes) -> FileBytes:
    ...


def force_no_attract_sound(data: Union[bytes, FileBytes]) -> Union[bytes, FileBytes]:
    return _patch_rom(data, bytes([0x40, 0x63, 0x12, 0xe2, 0xec, 0x32, 0x3c, 0x63, 0x09, 0x43]), bytes([0x00, 0xe3]))
