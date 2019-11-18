#! /usr/bin/env python3
from naomi.rom import NaomiRom


def _patch_rom(data: bytes, search: bytes, replace: bytes) -> bytes:
    rom = NaomiRom(data)
    if not rom.valid:
        raise Exception("ROM file does not appear to be a Naomi netboot ROM!")

    # Now, find the location of our magic string in the main executable.
    exec_data = rom.main_executable
    patch_location = None
    search_len = len(search)

    # Look through all copied sections.
    for section in exec_data.sections:
        for rloc in range(section.length - search_len):
            loc = rloc + section.offset
            if data[loc:(loc + search_len)] == search:
                patch_location = loc
                break
        if patch_location is not None:
            break

    if patch_location is None:
        raise Exception("ROM file does not have a suitable spot for the patch!")

    # Now, generate a patch with this updated data overlaid on the original rom
    return data[:patch_location] + replace + data[(patch_location + len(replace)):]


def force_freeplay(data: bytes) -> bytes:
    return _patch_rom(data, bytes([0x42, 0x84, 0xEC, 0x31, 0x0C, 0x60, 0x04, 0x1E, 0x43, 0x84]), bytes([0x1A, 0xE0]))


def force_no_attract_sound(data: bytes) -> bytes:
    return _patch_rom(data, bytes([0x40, 0x63, 0x12, 0xe2, 0xec, 0x32, 0x3c, 0x63, 0x09, 0x43]), bytes([0x00, 0xe3]))
