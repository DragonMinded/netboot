#!/usr/bin/env python3
import argparse
import os
import platform
import struct
import subprocess
import sys
import time
import yaml
from typing import Dict, List, Optional, Tuple

from arcadeutils import FileBytes
from naomi import NaomiRom, NaomiRomRegionEnum, add_or_update_section
from netdimm import NetDimm, NetDimmException, PeekPokeTypeEnum


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


MAX_PACKET_LENGTH: int = 253
MAX_EMPTY_READS: int = 10
MAX_FAILED_WRITES: int = 10
MENU_DATA_REGISTER: int = 0xC0DE10
MENU_SEND_STATUS_REGISTER: int = 0xC0DE20
MENU_RECV_STATUS_REGISTER: int = 0xC0DE30


def checksum_valid(data: int) -> bool:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF)
    return ((data >> 24) & 0xFF) == 0 and ((data >> 16) & 0xFF) == ((~sumval) & 0xFF)


def checksum_stamp(data: int) -> int:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF)
    return (((~sumval) & 0xFF) << 16) | (data & 0x0000FFFF)


def read_send_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            while status == 0 or status == 0xFFFFFFFF:
                status = netdimm.peek(MENU_SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status)

            if not valid and (time.time() - start > 1.0):
                return None

        return status


def write_send_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(MENU_SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value))


def read_recv_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            while status == 0 or status == 0xFFFFFFFF:
                status = netdimm.peek(MENU_RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status)

            if not valid and (time.time() - start > 1.0):
                return None

        return status


def write_recv_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(MENU_RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value))


def receive_packet(netdimm: NetDimm) -> Optional[bytes]:
    with netdimm.connection():
        # First, attempt to grab the next packet available.
        status = read_send_status_register(netdimm)
        if status is None:
            return None

        # Now, grab the length of the available packet.
        length = (status >> 8) & 0xFF
        if length == 0:
            return None

        # Now, see if the transfer was partially done, if so rewind it.
        loc = status & 0xFF
        if loc > 0:
            write_send_status_register(netdimm, 0)

        # Now, grab and assemble the data itself.
        data: List[Optional[int]] = [None] * length
        tries: int = 0
        while any(d is None for d in data):
            chunk = netdimm.peek(MENU_DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG)
            if ((chunk & 0xFF000000) >> 24) in {0x00, 0xFF}:
                tries += 1
                if tries > MAX_EMPTY_READS:
                    # We need to figure out where we left off.
                    for loc, val in enumerate(data):
                        if val is None:
                            # We found a spot to resume from.
                            write_send_status_register(netdimm, loc & 0xFF)
                            tries = 0
                            break
                    else:
                        # We should always find a spot to resume from or there's an issue,
                        # since in this case we should be done.
                        raise Exception("Logic error!")
            else:
                # Grab the location for this chunk, stick the data in the right spot.
                location = ((chunk >> 24) & 0xFF) - 1

                for off, shift in enumerate([16, 8, 0]):
                    actual = off + location
                    if actual < length:
                        data[actual] = (chunk >> shift) & 0xFF

        # Grab the actual return data.
        bytedata = bytes([d for d in data if d is not None])
        if len(bytedata) != length:
            raise Exception("Logic error!")

        # Acknowledge the data transfer completed.
        write_send_status_register(netdimm, length & 0xFF)

        # Return the actual data!
        return bytedata


def send_packet(netdimm: NetDimm, data: bytes) -> bool:
    length = len(data)
    if length > MAX_PACKET_LENGTH:
        raise Exception("Packet is too long to send!")

    with netdimm.connection():
        start = time.time()
        sent_length = False
        while True:
            if time.time() - start > 1.0:
                # Failed to request a new packet send in time.
                return False

            # First, attempt to see if there is any existing transfer in progress.
            status = read_recv_status_register(netdimm)
            if status is None:
                return False

            # Now, grab the length of the available packet.
            newlength = (status >> 8) & 0xFF
            if newlength == 0:
                # Ready to start transferring!
                write_recv_status_register(netdimm, (length << 8) & 0xFF00)
                sent_length = True
            elif sent_length is False or newlength != length:
                # Cancel old transfer.
                write_recv_status_register(netdimm, 0)
                sent_length = False
            elif newlength == length:
                # Ready to send data.
                break
            else:
                # Shouldn't be possible.
                raise Exception("Logic error!")

        # Now set the current transfer location. This can be rewound by the target
        # if it failed to receive all of the data.
        location = 0
        while True:
            while location < length:
                # Sum up the next amount of data, up to 3 bytes.
                chunk: int = (((location + 1) << 24) & 0xFF000000)

                for shift in [16, 8, 0]:
                    if location < length:
                        chunk |= (data[location] & 0xFF) << shift
                        location += 1
                    else:
                        break

                # Send it.
                netdimm.poke(MENU_DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG, chunk)

            # Now, see if the data transfer was successful.
            status = read_recv_status_register(netdimm)
            if status is None:
                # Give up, we can't read from the status.
                return False

            # See if the packet was sent successfully. If not, then our location will
            # be set to where the target needs data sent from.
            newlength = (status >> 8) & 0xFF
            location = status & 0xFF

            if newlength == 0 and location == 0:
                # We succeeded! Time to exit
                return True
            elif newlength != length:
                raise Exception("Logic error!")


class Message:
    def __init__(self, msgid: int, data: bytes = b"") -> None:
        self.id = msgid
        self.data = data


MESSAGE_HEADER_LENGTH: int = 8
MAX_MESSAGE_DATA_LENGTH: int = MAX_PACKET_LENGTH - MESSAGE_HEADER_LENGTH


send_sequence: int = 1


def send_message(netdimm: NetDimm, message: Message) -> bool:
    global send_sequence
    if send_sequence == 0:
        send_sequence = 1

    total_length = len(message.data)
    if total_length == 0:
        packetdata = struct.pack("<HHHH", message.id & 0xFFFF, send_sequence & 0xFFFF, 0, 0)
        if not send_packet(netdimm, packetdata):
            send_sequence = (send_sequence + 1) & 0xFFFF
            return False
    else:
        location = 0
        for chunk in [message.data[i:(i + MAX_MESSAGE_DATA_LENGTH)] for i in range(0, total_length, MAX_MESSAGE_DATA_LENGTH)]:
            packetdata = struct.pack("<HHHH", message.id & 0xFFFF, send_sequence & 0xFFFF, total_length & 0xFFFF, location & 0xFFFF) + chunk
            location += len(chunk)

            if not send_packet(netdimm, packetdata):
                send_sequence = (send_sequence + 1) & 0xFFFF
                return False

    send_sequence = (send_sequence + 1) & 0xFFFF
    return True


pending_received_chunks: Dict[int, Dict[int, bytes]] = {}


def receive_message(netdimm: NetDimm) -> Optional[Message]:
    # Try to receive a new packet.
    new_packet = receive_packet(netdimm)
    if new_packet is None:
        return None

    # Make sure it isn't a dud packet.
    if len(new_packet) < MESSAGE_HEADER_LENGTH:
        return None

    # See if this packet can be reassembled.
    msgid, sequence, total_length, location = struct.unpack("<HHHH", new_packet[0:8])

    if sequence not in pending_received_chunks:
        pending_received_chunks[sequence] = {}
    if location not in pending_received_chunks[sequence]:
        pending_received_chunks[sequence][location] = new_packet[8:]

    for needed_location in range(0, total_length, MAX_MESSAGE_DATA_LENGTH):
        if needed_location not in pending_received_chunks[sequence]:
            # We're missing this location.
            return None

    # We have it all!
    msgdata = b"".join(pending_received_chunks[sequence][position] for position in range(0, total_length, MAX_MESSAGE_DATA_LENGTH))
    del pending_received_chunks[sequence]

    return Message(msgid, msgdata)


class Settings:
    def __init__(
        self,
        last_game_file: Optional[str],
        enable_analog: bool,
    ):
        self.last_game_file = last_game_file
        self.enable_analog = enable_analog


def settings_load(settings_file: str, ip: str) -> Settings:
    try:
        with open(settings_file, "r") as fp:
            data = yaml.safe_load(fp)
    except FileNotFoundError:
        data = {}

    settings = Settings(
        last_game_file=None,
        enable_analog=False,
    )

    if isinstance(data, dict):
        if ip in data:
            data = data[ip]

            if isinstance(data, dict):
                if 'enable_analog' in data:
                    settings.enable_analog = bool(data['enable_analog'])
                if 'last_game_file' in data:
                    settings.last_game_file = str(data['last_game_file']) if data['last_game_file'] is not None else None

    return settings


def settings_save(settings_file: str, ip: str, settings: Settings) -> None:
    try:
        with open(settings_file, "r") as fp:
            data = yaml.safe_load(fp)
    except FileNotFoundError:
        data = {}

    data[ip] = {
        'enable_analog': settings.enable_analog,
        'last_game_file': settings.last_game_file,
    }

    with open(settings_file, "w") as fp:
        yaml.dump(data, fp)


MESSAGE_SELECTION: int = 0x1000


def main() -> int:
    parser = argparse.ArgumentParser(description="Provide an on-target menu for selecting games. Currently only works with Naomi.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        "romdir",
        metavar="ROMDIR",
        type=str,
        default=os.path.join(root, 'roms'),
        help='The directory of ROMs to select a game from. Defaults to %(default)s.',
    )
    parser.add_argument(
        '--region',
        metavar="REGION",
        type=str,
        help='The region of the Naomi which we are running the menu on. Defaults to "japan".',
    )
    parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew', 'netbootmenu', 'netbootmenu.bin'),
        help='The menu executable that we should send to display games on the Naomi. Defaults to %(default)s.',
    )
    parser.add_argument(
        '--menu-settings-file',
        metavar='SETTINGS',
        type=str,
        default=os.path.join(root, '.netdimm_menu_settings.yaml'),
        help='The settings file we will use to store persistent settings. Defaults to %(default)s.',
    )
    parser.add_argument(
        '--force-analog',
        action="store_true",
        help="Force-enable analog control inputs. Use this if you have no digital controls and cannot set up analog options in the test menu.",
    )
    parser.add_argument(
        '--persistent',
        action="store_true",
        help="Don't exit after successfully booting game. Instead, wait for power cycle and then send the menu again.",
    )
    parser.add_argument(
        '--debug-mode',
        action="store_true",
        help="Enable extra debugging information on the Naomi.",
    )
    parser.add_argument(
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    args = parser.parse_args()
    verbose = args.verbose

    region = {
        "japan": NaomiRomRegionEnum.REGION_JAPAN,
        "usa": NaomiRomRegionEnum.REGION_USA,
        "export": NaomiRomRegionEnum.REGION_EXPORT,
        "korea": NaomiRomRegionEnum.REGION_KOREA,
        "australia": NaomiRomRegionEnum.REGION_AUSTRALIA,
    }.get(args.region, NaomiRomRegionEnum.REGION_JAPAN)

    # Load the settings file
    settings = settings_load(args.menu_settings_file, args.ip)

    if args.force_analog:
        # Force the setting on, as a safeguard against cabinets that have no digital controls.
        settings.enable_analog = True
        settings_save(args.menu_settings_file, args.ip, settings)

    # Intentionally rebuild the menu every loop if we are in persistent mode, so that
    # changes to the ROM directory can be reflected on subsequent menu sends.
    while True:
        # First, load the rom directory, list out the contents and figure out which ones are naomi games.
        games: List[Tuple[str, str, bytes]] = []
        romdir = os.path.abspath(args.romdir)
        success: bool = True
        for filename in [f for f in os.listdir(romdir) if os.path.isfile(os.path.join(romdir, f))]:
            # Grab the header so we can parse it.
            with open(os.path.join(romdir, filename), "rb") as fp:
                data = FileBytes(fp)

                if verbose:
                    print(f"Discovered file {filename}.")

                # Validate that it is a Naomi ROM.
                if len(data) < NaomiRom.HEADER_LENGTH:
                    if verbose:
                        print("Not long enough to be a ROM!")
                    continue
                rom = NaomiRom(data)
                if not rom.valid:
                    if verbose:
                        print("Not a Naomi ROM!")
                    continue

                # Get the name of the game.
                name = rom.names[region]
                serial = rom.serial

                if verbose:
                    print(f"Added {name} with serial {serial.decode('ascii')} to ROM list.")

                games.append((os.path.join(romdir, filename), name, serial))

        # Alphabetize them.
        games = sorted(games, key=lambda g: g[1])

        # Now, create the settings section.
        last_game_id: int = 0
        gamesconfig = b""
        for index, (filename, name, serial) in enumerate(games):
            namebytes = name.encode('ascii')[:127]
            while len(namebytes) < 128:
                namebytes = namebytes + b"\0"
            gamesconfig += namebytes + serial + struct.pack("<I", index)
            if filename == settings.last_game_file:
                last_game_id = index

        config = struct.pack("<IIIII", 20, len(games), 1 if settings.enable_analog else 0, 1 if args.debug_mode else 0, last_game_id) + gamesconfig

        # Now, load up the menu ROM and append the settings to it.
        if success:
            with open(args.exe, "rb") as fp:
                menudata = add_or_update_section(FileBytes(fp), 0x0D000000, config, verbose=verbose)

                try:
                    # Now, connect to the net dimm, send the menu and then start communicating with it.
                    print("Connecting to net dimm...")
                    netdimm = NetDimm(args.ip, log=print if verbose else None)
                    print("Sending menu to net dimm...")
                    netdimm.send(menudata, disable_crc_check=True)
                    netdimm.reboot()
                except NetDimmException:
                    # Mark failure so we don't try to communicate below.
                    success = False

                    if args.persistent:
                        print("Sending failed, will try again...")
                    else:
                        print("Sending failed...")

        # Now, talk to the net dimm and exchange packets to handle settings and game selection.
        if success:
            print("Talking to net dimm to wait for ROM selection...")

            try:
                # Always show game send progress.
                netdimm = NetDimm(args.ip, log=print)
                while True:
                    msg = receive_message(netdimm)
                    if msg:
                        if verbose:
                            print(f"Received type: {hex(msg.id)}, length: {len(msg.data)}")
                        if msg.id == MESSAGE_SELECTION:
                            index = struct.unpack("<I", msg.data)[0]
                            filename = games[index][0]
                            print(f"Requested {games[index][1]} be loaded...")

                            # Save the menu position.
                            settings.last_game_file = filename
                            settings_save(args.menu_settings_file, args.ip, settings)

                            with open(filename, "rb") as fp:
                                gamedata = FileBytes(fp)
                                netdimm.send(gamedata, disable_crc_check=True)
                                netdimm.reboot()
                            break
            except NetDimmException:
                # Mark failure so we don't try to wait for power down below.
                success = False

                if args.persistent:
                    print("Communicating failed, will try again...")
                else:
                    print("Communicating failed...")

        if args.persistent:
            if success:
                # Wait for cabinet to disappear again before we start the process over.
                print("Waiting for cabinet to be power cycled to resend menu...")
                failure_count: int = 0
                on_windows: bool = platform.system() == "Windows"

                while True:
                    # Constantly ping the net dimm to see if it is still alive.
                    with open(os.devnull, 'w') as DEVNULL:
                        try:
                            if on_windows:
                                call = ["ping", "-n", "1", "-w", "1", args.ip]
                            else:
                                call = ["ping", "-c1", "-W1", args.ip]
                            subprocess.check_call(call, stdout=DEVNULL, stderr=DEVNULL)
                            alive = True
                        except subprocess.CalledProcessError:
                            alive = False

                    # We start with the understanding that the host is up, but if we
                    # miss a ping its not that big of a deal. We just want to know that
                    # we missed multiple pings as that tells us the host is truly gone.
                    if alive:
                        failure_count = 0
                    else:
                        failure_count += 1
                        if failure_count >= 5:
                            # We failed 5 pings in a row, so let's assume the host is
                            # dead.
                            break

                    time.sleep(2 if failure_count == 0 else 1)

            # Now, wait for the cabinet to come back so we can send the menu again.
            print("Waiting for cabinet to be ready to receive the menu...")
            while True:
                try:
                    netdimm = NetDimm(args.ip, log=print if verbose else None)
                    info = netdimm.info()
                    if info is not None:
                        break
                except NetDimmException:
                    # Failed to talk to the net dimm, its still down.
                    pass
        else:
            # We sent the game, now exit!
            break

    return 0


if __name__ == "__main__":
    sys.exit(main())
