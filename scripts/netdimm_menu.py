#!/usr/bin/env python3
import argparse
import os
import platform
import struct
import subprocess
import sys
import time
import yaml
from typing import Dict, List, Optional, Set, Tuple

from arcadeutils import FileBytes, BinaryDiff
from naomi import NaomiRom, NaomiRomRegionEnum, NaomiSettingsPatcher, NaomiSettingsTypeEnum, get_default_trojan, add_or_update_section
from naomi.settings import SettingsManager, Setting, ReadOnlyCondition, SettingsWrapper, get_default_settings_directory
from netdimm import NetDimm, NetDimmException, Message, send_message, receive_message, write_scratch1_register, MESSAGE_HOST_STDOUT, MESSAGE_HOST_STDERR
from netboot import PatchManager


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


class GameSettings:
    def __init__(
        self,
        enabled_patches: Set[str],
        force_settings: bool,
        eeprom: Optional[bytes],
    ):
        self.enabled_patches = enabled_patches
        self.force_settings = force_settings
        self.eeprom = eeprom

    @staticmethod
    def default() -> "GameSettings":
        return GameSettings(
            enabled_patches=set(),
            force_settings=False,
            eeprom=None,
        )


class Settings:
    def __init__(
        self,
        last_game_file: Optional[str],
        enable_analog: bool,
        system_region: NaomiRomRegionEnum,
        use_filenames: bool,
        disable_sound: bool,
        joy1_calibration: List[int],
        joy2_calibration: List[int],
        game_settings: Dict[str, GameSettings],
    ):
        self.last_game_file = last_game_file
        self.enable_analog = enable_analog
        self.system_region = system_region
        self.use_filenames = use_filenames
        self.disable_sound = disable_sound
        self.joy1_calibration = joy1_calibration
        self.joy2_calibration = joy2_calibration
        self.game_settings = game_settings


def settings_load(settings_file: str, ip: str) -> Settings:
    try:
        with open(settings_file, "r") as fp:
            data = yaml.safe_load(fp)
    except FileNotFoundError:
        data = {}

    settings = Settings(
        last_game_file=None,
        enable_analog=False,
        system_region=NaomiRomRegionEnum.REGION_JAPAN,
        use_filenames=False,
        disable_sound=False,
        joy1_calibration=[0x80, 0x80, 0x50, 0xB0, 0x50, 0xB0],
        joy2_calibration=[0x80, 0x80, 0x50, 0xB0, 0x50, 0xB0],
        game_settings={},
    )

    if isinstance(data, dict):
        if ip in data:
            data = data[ip]

            if isinstance(data, dict):
                if 'enable_analog' in data:
                    settings.enable_analog = bool(data['enable_analog'])
                if 'disable_sound' in data:
                    settings.disable_sound = bool(data['disable_sound'])
                if 'last_game_file' in data:
                    settings.last_game_file = str(data['last_game_file']) if data['last_game_file'] is not None else None
                if 'use_filenames' in data:
                    settings.use_filenames = bool(data['use_filenames'])
                if 'system_region' in data:
                    try:
                        settings.system_region = NaomiRomRegionEnum(data['system_region'])
                    except ValueError:
                        pass
                if 'joy1_calibration' in data:
                    calib = data['joy1_calibration']
                    if isinstance(calib, list) and len(calib) == 6:
                        settings.joy1_calibration = calib
                if 'joy2_calibration' in data:
                    calib = data['joy2_calibration']
                    if isinstance(calib, list) and len(calib) == 6:
                        settings.joy2_calibration = calib
                if 'game_settings' in data:
                    gs = data['game_settings']
                    if isinstance(gs, dict):
                        for game, gamesettings in gs.items():
                            settings.game_settings[game] = GameSettings.default()
                            if isinstance(gamesettings, dict):
                                if 'enabled_patches' in gamesettings:
                                    enabled_patches = gamesettings['enabled_patches']
                                    if isinstance(enabled_patches, list):
                                        settings.game_settings[game].enabled_patches = {str(p) for p in enabled_patches}
                                if 'eeprom' in gamesettings:
                                    eeprom = gamesettings['eeprom']
                                    if isinstance(eeprom, list) and len(eeprom) == 128:
                                        settings.game_settings[game].eeprom = bytes(eeprom)
                                if 'force_settings' in gamesettings:
                                    settings.game_settings[game].force_settings = bool(gamesettings['force_settings'])

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
        'use_filenames': settings.use_filenames,
        'system_region': settings.system_region.value,
        'disable_sound': settings.disable_sound,
        'joy1_calibration': settings.joy1_calibration,
        'joy2_calibration': settings.joy2_calibration,
        'game_settings': {},
    }

    for game, gamesettings in settings.game_settings.items():
        gamedict = {
            'enabled_patches': list(gamesettings.enabled_patches),
            'force_settings': gamesettings.force_settings,
            'eeprom': [x for x in gamesettings.eeprom] if gamesettings.eeprom is not None else None,
        }
        data[ip]['game_settings'][game] = gamedict

    with open(settings_file, "w") as fp:
        yaml.dump(data, fp)


MESSAGE_SELECTION: int = 0x1000
MESSAGE_LOAD_SETTINGS: int = 0x1001
MESSAGE_LOAD_SETTINGS_ACK: int = 0x1002
MESSAGE_SAVE_CONFIG: int = 0x1003
MESSAGE_SAVE_CONFIG_ACK: int = 0x1004
MESSAGE_LOAD_SETTINGS_DATA: int = 0x1005
MESSAGE_SAVE_SETTINGS_DATA: int = 0x1007
MESSAGE_SAVE_SETTINGS_ACK: int = 0x1008
MESSAGE_LOAD_PROGRESS: int = 0x1009
MESSAGE_UNPACK_PROGRESS: int = 0x100A

SETTINGS_SIZE: int = 64

READ_ONLY_ALWAYS: int = -1
READ_ONLY_NEVER: int = -2


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
        default=None,
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
        "--patchdir",
        metavar="PATCHDIR",
        type=str,
        default=os.path.join(root, 'patches'),
        help='The directory of patches we might want to apply to games. Defaults to %(default)s.',
    )
    parser.add_argument(
        '--force-analog',
        action="store_true",
        help="Force-enable analog control inputs. Use this if you have no digital controls and cannot set up analog options in the test menu.",
    )
    parser.add_argument(
        '--force-players',
        type=int,
        default=0,
        help="Force set the number of players for this cabinet. Valid values are 1-4. Use this if you do not want to set the player number in the system test menu.",
    )
    parser.add_argument(
        '--force-use-filenames',
        action="store_true",
        help="Force-enable using filenames for ROM display instead of the name in the ROM.",
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
        '--fallback-font',
        metavar="FILE",
        type=str,
        default=None,
        help="Any truetype font that should be used as a fallback if the built-in font can't render a character.",
    )
    parser.add_argument(
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    args = parser.parse_args()
    verbose = args.verbose

    # Load the settings file
    settings = settings_load(args.menu_settings_file, args.ip)

    if args.region is not None:
        region = {
            "japan": NaomiRomRegionEnum.REGION_JAPAN,
            "usa": NaomiRomRegionEnum.REGION_USA,
            "export": NaomiRomRegionEnum.REGION_EXPORT,
            "korea": NaomiRomRegionEnum.REGION_KOREA,
        }.get(args.region, NaomiRomRegionEnum.REGION_JAPAN)
        settings.system_region = region
        settings_save(args.menu_settings_file, args.ip, settings)

    if args.force_analog:
        # Force the setting on, as a safeguard against cabinets that have no digital controls.
        settings.enable_analog = True
        settings_save(args.menu_settings_file, args.ip, settings)

    if args.force_use_filenames:
        settings.use_filenames = True
        settings_save(args.menu_settings_file, args.ip, settings)

    force_players = None
    if args.force_players is not None:
        if args.force_players >= 1 and args.force_players <= 4:
            force_players = args.force_players

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
                if settings.use_filenames:
                    name = os.path.splitext(filename)[0].replace("_", " ")
                else:
                    name = rom.names[settings.system_region]
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
            namebytes = name.encode('utf-8')[:127]
            while len(namebytes) < 128:
                namebytes = namebytes + b"\0"
            gamesconfig += namebytes + serial + struct.pack("<I", index)
            if filename == settings.last_game_file:
                last_game_id = index

        fallback_data = None
        if args.fallback_font is not None:
            with open(args.fallback_font, "rb") as fp:
                fallback_data = fp.read()

        config = struct.pack(
            "<IIIIIIIIBBBBBBBBBBBBIII",
            SETTINGS_SIZE,
            len(games),
            1 if settings.enable_analog else 0,
            1 if args.debug_mode else 0,
            last_game_id,
            settings.system_region.value,
            1 if settings.use_filenames else 0,
            1 if settings.disable_sound else 0,
            settings.joy1_calibration[0],
            settings.joy1_calibration[1],
            settings.joy2_calibration[0],
            settings.joy2_calibration[1],
            settings.joy1_calibration[2],
            settings.joy1_calibration[3],
            settings.joy1_calibration[4],
            settings.joy1_calibration[5],
            settings.joy2_calibration[2],
            settings.joy2_calibration[3],
            settings.joy2_calibration[4],
            settings.joy2_calibration[5],
            SETTINGS_SIZE + len(gamesconfig) if fallback_data is not None else 0,
            len(fallback_data) if fallback_data is not None else 0,
            force_players if (force_players is not None) else 0,
        )
        if len(config) < SETTINGS_SIZE:
            config = config + (b"\0" * (SETTINGS_SIZE - len(config)))
        config = config + gamesconfig
        if fallback_data is not None:
            config = config + fallback_data

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
        selected_file = None

        if success:
            print("Talking to net dimm to wait for ROM selection...")
            time.sleep(5)

            last_game_selection: Optional[int] = None
            last_game_patches: List[Tuple[str, str]] = []
            last_game_parsed_settings: Optional[SettingsWrapper] = None

            try:
                # Always show game send progress.
                netdimm = NetDimm(args.ip, log=print)
                with netdimm.connection():
                    while True:
                        msg = receive_message(netdimm, verbose=verbose)
                        if msg:
                            if msg.id == MESSAGE_SELECTION:
                                index = struct.unpack("<I", msg.data)[0]
                                filename = games[index][0]
                                print(f"Requested {games[index][1]} be loaded...")

                                # Save the menu position.
                                settings.last_game_file = filename
                                settings_save(args.menu_settings_file, args.ip, settings)

                                # Wait a second for animation on the Naomi. This assumes that the
                                # below section takes a relatively short amount of time (well below
                                # about 1 second) to patch and such. If you are on a platform with
                                # limited speed and attempting to do extra stuff such as unzipping,
                                # this can fail. It is recommended in this case to spawn off a new
                                # thread that sends a MESSAGE_UNPACK_PROGRESS with no data once a
                                # second starting directly after this time.sleep() call. When you
                                # are finished patching and ready to send, kill the thread before
                                # the MESSAGE_LOAD_PROGRESS message is sent below and then let the
                                # message send normally.
                                time.sleep(1.0)

                                # First, grab a handle to the data itself.
                                fp = open(filename, "rb")
                                gamedata = FileBytes(fp)
                                gamesettings = settings.game_settings.get(filename, GameSettings.default())

                                # Now, patch with selected patches.
                                patchman = PatchManager([args.patchdir])
                                for patchfile in gamesettings.enabled_patches:
                                    print(f"Applying patch {patchman.patch_name(patchfile)} to game...")
                                    with open(patchfile, "r") as pp:
                                        differences = pp.readlines()
                                        differences = [d.strip() for d in differences if d.strip()]
                                        try:
                                            gamedata = BinaryDiff.patch(gamedata, differences)
                                        except Exception as e:
                                            print(f"Could not patch {filename} with {patchfile}: {str(e)}", file=sys.stderr)

                                # Now, attach any eeprom settings.
                                if gamesettings.force_settings and gamesettings.eeprom is not None:
                                    patcher = NaomiSettingsPatcher(gamedata, get_default_trojan())
                                    if patcher.type != NaomiSettingsTypeEnum.TYPE_SRAM:
                                        print(f"Applying EEPROM settings to {filename}...")
                                        try:
                                            patcher.put_settings(
                                                gamesettings.eeprom,
                                                enable_debugging=args.debug_mode,
                                                verbose=verbose,
                                            )
                                            gamedata = patcher.data
                                        except Exception as e:
                                            print(f"Could not apply EEPROM settings to {filename}: {str(e)}", file=sys.stderr)

                                # Finally, send it!
                                send_message(netdimm, Message(MESSAGE_LOAD_PROGRESS, struct.pack("<ii", len(gamedata), 0)), verbose=verbose)
                                selected_file = gamedata
                                break

                            elif msg.id == MESSAGE_LOAD_SETTINGS:
                                index = struct.unpack("<I", msg.data)[0]
                                filename = games[index][0]
                                print(f"Requested settings for {games[index][1]}...")
                                send_message(netdimm, Message(MESSAGE_LOAD_SETTINGS_ACK, msg.data), verbose=verbose)

                                # Grab the configured settings for this game.
                                gamesettings = settings.game_settings.get(filename, GameSettings.default())
                                last_game_selection = index

                                # First, gather up the patches which might be applicable.
                                patchman = PatchManager([args.patchdir])
                                patchfiles = patchman.patches_for_game(filename)
                                patches = sorted([(p, patchman.patch_name(p)) for p in patchfiles], key=lambda p: p[1])
                                last_game_patches = patches

                                # Grab any EEPROM settings which might be applicable.
                                parsedsettings = None
                                with open(filename, "rb") as fp:
                                    data = FileBytes(fp)

                                    eepromdata = gamesettings.eeprom
                                    has_settings = True
                                    if eepromdata is None:
                                        # Possibly they edited the ROM directly, still let them edit the settings.
                                        patcher = NaomiSettingsPatcher(data, get_default_trojan())
                                        if patcher.type == NaomiSettingsTypeEnum.TYPE_EEPROM:
                                            eepromdata = patcher.get_settings()
                                        elif patcher.type == NaomiSettingsTypeEnum.TYPE_SRAM:
                                            has_settings = False

                                    if has_settings:
                                        manager = SettingsManager(get_default_settings_directory())
                                        if eepromdata is None:
                                            # We need to make them up from scratch.
                                            parsedsettings = manager.from_rom(patcher.rom, region=settings.system_region)
                                        else:
                                            # We have an eeprom to edit.
                                            parsedsettings = manager.from_eeprom(eepromdata)

                                # Now, create the message back to the Naomi.
                                response = struct.pack("<IB", index, len(patches))
                                for patch in patches:
                                    response += struct.pack("<B", 1 if (patch[0] in gamesettings.enabled_patches) else 0)
                                    patchname = patch[1].encode('utf-8')[:255]
                                    response += struct.pack("<B", len(patchname)) + patchname

                                def make_setting(setting: Setting, setting_map: Dict[str, int]) -> bytes:
                                    if setting.read_only is True:
                                        # We don't encode this setting since its not visible.
                                        return struct.pack("<BI", 0, setting.current or setting.default or 0)

                                    settingname = setting.name.encode('utf-8')[:255]
                                    if len(settingname) == 0:
                                        # We can't display this setting, it has no name!
                                        return struct.pack("<BI", 0, setting.current or setting.default or 0)

                                    settingdata = struct.pack("<B", len(settingname)) + settingname
                                    if setting.values is not None:
                                        settingdata += struct.pack("<I", len(setting.values))
                                        for val, label in setting.values.items():
                                            settingdata += struct.pack("<I", val)
                                            valname = label.encode('utf-8')[:255]
                                            settingdata += struct.pack("<B", len(valname)) + valname
                                    else:
                                        settingdata += struct.pack("<I", 0)

                                    settingdata += struct.pack("<I", setting.current or setting.default or 0)

                                    if setting.read_only is True:
                                        settingdata += struct.pack("<i", READ_ONLY_ALWAYS)
                                    elif setting.read_only is False:
                                        settingdata += struct.pack("<i", READ_ONLY_NEVER)
                                    elif isinstance(setting.read_only, ReadOnlyCondition):
                                        settingdata += struct.pack("<iII", setting_map[setting.read_only.name], 1 if setting.read_only.negate else 0, len(setting.read_only.values))
                                        for val in setting.read_only.values:
                                            settingdata += struct.pack("<I", val)
                                    else:
                                        raise Exception("Logic error!")

                                    return settingdata

                                if has_settings and parsedsettings is not None:
                                    # Remember the settings we parsed so we can save them later.
                                    last_game_parsed_settings = parsedsettings

                                    # Now add data for the force settings toggle.
                                    totalsettings = len(parsedsettings.system.settings) + len(parsedsettings.game.settings)
                                    response += struct.pack("<B", 1 if (totalsettings > 0 and gamesettings.force_settings) else 0)

                                    # Construct system settings.
                                    response += struct.pack("<B", len(parsedsettings.system.settings))
                                    for setting in parsedsettings.system.settings:
                                        response += make_setting(setting, {s.name: i for (i, s) in enumerate(parsedsettings.system.settings)})

                                    # Construct game settings
                                    response += struct.pack("<B", len(parsedsettings.game.settings))
                                    for setting in parsedsettings.game.settings:
                                        response += make_setting(setting, {s.name: i for (i, s) in enumerate(parsedsettings.game.settings)})
                                else:
                                    # This game has a SRAM chunk attached (atomiswave game), don't try to send settings.
                                    response += struct.pack("<BBB", 0, 0, 0)

                                # Send settings over.
                                send_message(netdimm, Message(MESSAGE_LOAD_SETTINGS_DATA, response), verbose=verbose)

                            elif msg.id == MESSAGE_SAVE_SETTINGS_DATA:
                                index, patchlen = struct.unpack("<IB", msg.data[0:5])
                                msgdata = msg.data[5:]

                                if index == last_game_selection:
                                    filename = games[index][0]
                                    gamesettings = settings.game_settings.get(filename, GameSettings.default())
                                    last_game_selection = None

                                    print(f"Received updated settings for {games[index][1]}...")

                                    # Grab the updated patches.
                                    if patchlen > 0:
                                        patches_enabled = list(struct.unpack("<" + ("B" * patchlen), msgdata[0:(1 * patchlen)]))
                                        msgdata = msgdata[(1 * patchlen):]

                                        if patchlen == len(last_game_patches):
                                            new_patches: Set[str] = set()
                                            for i in range(patchlen):
                                                if patches_enabled[i] != 0:
                                                    new_patches.add(last_game_patches[i][0])
                                            gamesettings.enabled_patches = new_patches
                                    last_game_patches = []

                                    # Grab system settings.
                                    force_settings, settinglen = struct.unpack("<BB", msgdata[0:2])
                                    msgdata = msgdata[2:]

                                    if settinglen > 0:
                                        settings_values = list(struct.unpack("<" + ("I" * settinglen), msgdata[0:(4 * settinglen)]))
                                        msgdata = msgdata[(4 * settinglen):]

                                        if last_game_parsed_settings is not None:
                                            if len(settings_values) == len(last_game_parsed_settings.system.settings):
                                                for i, setting in enumerate(last_game_parsed_settings.system.settings):
                                                    setting.current = settings_values[i]

                                    # Grab game settings.
                                    settinglen = struct.unpack("<B", msgdata[0:1])[0]
                                    msgdata = msgdata[1:]

                                    if settinglen > 0:
                                        settings_values = list(struct.unpack("<" + ("I" * settinglen), msgdata[0:(4 * settinglen)]))
                                        msgdata = msgdata[(4 * settinglen):]

                                        if last_game_parsed_settings is not None:
                                            if len(settings_values) == len(last_game_parsed_settings.game.settings):
                                                for i, setting in enumerate(last_game_parsed_settings.game.settings):
                                                    setting.current = settings_values[i]

                                    if last_game_parsed_settings is not None:
                                        manager = SettingsManager(get_default_settings_directory())
                                        gamesettings.eeprom = manager.to_eeprom(last_game_parsed_settings)
                                        gamesettings.force_settings = force_settings != 0
                                    else:
                                        gamesettings.force_settings = False

                                    last_game_parsed_settings = None

                                    # Save the final updates.
                                    settings.game_settings[filename] = gamesettings
                                    settings_save(args.menu_settings_file, args.ip, settings)
                                send_message(netdimm, Message(MESSAGE_SAVE_SETTINGS_ACK), verbose=verbose)

                            elif msg.id == MESSAGE_SAVE_CONFIG:
                                if len(msg.data) == SETTINGS_SIZE:
                                    (
                                        _,
                                        _,
                                        analogsetting,
                                        _,
                                        _,
                                        regionsetting,
                                        filenamesetting,
                                        soundsetting,
                                        *rest,
                                    ) = struct.unpack("<IIIIIIIIBBBBBBBBBBBB", msg.data[:44])
                                    print("Requested configuration save...")

                                    joy1 = [rest[0], rest[1], rest[4], rest[5], rest[6], rest[7]]
                                    joy2 = [rest[2], rest[3], rest[8], rest[9], rest[10], rest[11]]

                                    settings.enable_analog = analogsetting != 0
                                    settings.use_filenames = filenamesetting != 0
                                    settings.system_region = NaomiRomRegionEnum(regionsetting)
                                    settings.disable_sound = soundsetting != 0
                                    settings.joy1_calibration = joy1
                                    settings.joy2_calibration = joy2
                                    settings_save(args.menu_settings_file, args.ip, settings)

                                    send_message(netdimm, Message(MESSAGE_SAVE_CONFIG_ACK), verbose=verbose)
                            elif msg.id == MESSAGE_HOST_STDOUT:
                                print(msg.data.decode('utf-8'), end="")
                            elif msg.id == MESSAGE_HOST_STDERR:
                                print(msg.data.decode('utf-8'), end="", file=sys.stderr)

            except NetDimmException:
                # Mark failure so we don't try to wait for power down below.
                success = False

                if args.persistent:
                    print("Communicating failed, will try again...")
                else:
                    print("Communicating failed...")

        if success and selected_file is not None:
            try:
                # Always show game send progress.
                netdimm = NetDimm(args.ip, log=print)

                # Only want to send so many progress packets.
                old_percent = -1
                old_time = time.time()

                def progress_callback(loc: int, size: int) -> None:
                    nonlocal old_percent
                    nonlocal old_time

                    new_percent = int((loc / size) * 100)
                    new_time = time.time()
                    if new_percent != old_percent or (new_time - old_time) > 2.0:
                        write_scratch1_register(netdimm, loc)
                        old_percent = new_percent
                        old_time = new_time

                # Finally, send it!.
                netdimm.send(selected_file, disable_crc_check=True, disable_now_loading=True, progress_callback=progress_callback)
                netdimm.reboot()

                # And clean up.
                selected_file.handle.close()
                selected_file = None
            except NetDimmException as e:
                # Mark failure so we don't try to wait for power down below.
                print(str(e))
                success = False

                if args.persistent:
                    print("Sending game failed, will try again...")
                else:
                    print("Sending game failed...")

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

                    time.sleep(2.0 if failure_count == 0 else 1.0)

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
