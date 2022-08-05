import ipaddress
import os.path
import tempfile
import threading
import time
import yaml
from cachetools import TTLCache
from enum import Enum
from typing import Dict, List, Optional, Sequence, Tuple, Union, cast

from naomi import NaomiSettingsPatcher
from netdimm import NetDimmInfo, NetDimmException, NetDimmVersionEnum, NetDimmTargetEnum, CRCStatusEnum
from netboot.hostutils import Host, HostStatusEnum, SettingsEnum
from netboot.log import log
from outlet import OutletInterface, ALL_OUTLET_CLASSES


class CabinetException(Exception):
    pass


class CabinetStateEnum(Enum):
    STATE_TURNED_OFF = "turned_off"
    STATE_STARTUP = "startup"
    STATE_WAIT_FOR_CABINET_POWER_ON = "wait_power_on"
    STATE_SEND_CURRENT_GAME = "send_game"
    STATE_CHECK_CURRENT_GAME = "check_game"
    STATE_WAIT_FOR_CABINET_POWER_OFF = "wait_power_off"
    STATE_POWER_CYCLE = "power_cycle"
    STATE_DISABLED = "disabled"


class CabinetRegionEnum(Enum):
    REGION_UNKNOWN = "unknown"
    REGION_JAPAN = "japan"
    REGION_USA = "usa"
    REGION_EXPORT = "export"
    REGION_KOREA = "korea"
    REGION_AUSTRALIA = "australia"


class CabinetPowerStateEnum(Enum):
    POWER_OFF = "off"
    POWER_ON = "on"
    POWER_DISABLED = "disabled"
    POWER_UNKNOWN = "unknown"


class Cabinet:
    REBOOT_LENGTH: int = 3

    def __init__(
        self,
        ip: str,
        region: CabinetRegionEnum,
        description: str,
        filename: Optional[str],
        patches: Dict[str, Sequence[str]],
        settings: Dict[str, Optional[bytes]],
        srams: Dict[str, Optional[str]],
        outlet: Optional[Dict[str, object]],
        target: Optional[NetDimmTargetEnum] = None,
        version: Optional[NetDimmVersionEnum] = None,
        send_timeout: Optional[int] = None,
        time_hack: bool = False,
        enabled: bool = True,
        controllable: bool = True,
        power_cycle: bool = False,
        quiet: bool = False,
    ) -> None:
        self.description: str = description
        self.region: CabinetRegionEnum = region
        self.patches: Dict[str, List[str]] = {rom: [p for p in patches[rom]] for rom in patches}
        self.settings: Dict[str, Optional[bytes]] = {rom: settings[rom] for rom in settings}
        self.srams: Dict[str, Optional[str]] = {rom: srams[rom] for rom in srams}
        self.quiet = quiet
        self.power_cycle = power_cycle
        self.__enabled = enabled
        self.__host: Host = Host(ip, target=target, version=version, send_timeout=send_timeout, time_hack=time_hack, quiet=self.quiet)
        self.__lock: threading.Lock = threading.Lock()
        self.__current_filename: Optional[str] = filename
        self.__new_filename: Optional[str] = filename
        self.__state: Tuple[CabinetStateEnum, int] = (CabinetStateEnum.STATE_STARTUP, 0)
        self.__outlet: Optional[OutletInterface] = self.__spawn_outlet_interface(outlet)
        self.__cache: TTLCache[str, object] = TTLCache(maxsize=10, ttl=2)
        self.__controllable: bool = controllable
        self.__statefile: str = os.path.join(tempfile.gettempdir(), f"{ip}.reboot")

    def __spawn_outlet_interface(self, outlet: Optional[Dict[str, object]]) -> Optional[OutletInterface]:
        if outlet is None:
            return None
        if 'type' not in outlet:
            return None
        for impl in ALL_OUTLET_CLASSES:
            if impl.type == outlet['type']:
                data = {x: y for x, y in outlet.items() if x != 'type'}
                try:
                    return impl(**data)
                except TypeError:
                    return None
        return None

    def __get_reboot_time(self) -> Optional[int]:
        # We store the reboot time on the filesystem instead of in memory just in case
        # we get rebooted or lose power mid-cycle. If we didn't store that, we would have
        # no way of knowing the difference between a cabinet that was turned off on purpose
        # and one that was turned off during a power cycle sequence that was interrupted.
        # That would mean that if cabinet user control was disabled and somebody sent a game
        # and we crashed or something, then the cabinet has the chance of just turning off.
        if not os.path.isfile(self.__statefile):
            return None
        try:
            with open(self.__statefile, "rb") as fp:
                data = fp.read().decode('utf-8')
                try:
                    return int(data)
                except (TypeError, ValueError):
                    return None
        except (FileNotFoundError, PermissionError):
            return None

    def __set_reboot_time(self, time: Optional[int]) -> None:
        if time is None:
            try:
                os.remove(self.__statefile)
            except (FileNotFoundError, PermissionError):
                return
        else:
            try:
                with open(self.__statefile, "wb") as fp:
                    fp.write(str(time).encode('utf-8'))
            except (FileNotFoundError, PermissionError):
                return

    def __repr__(self) -> str:
        with self.__lock:
            return f"Cabinet(ip={repr(self.ip)}, enabled={repr(self.__enabled)}, time_hack={repr(self.time_hack)}, send_timeout={repr(self.send_timeout)}, description={repr(self.description)}, filename={repr(self.filename)}, patches={repr(self.patches)}, settings={repr(self.settings)}, srams={repr(self.srams)}, target={repr(self.target)}, version={repr(self.version)})"

    @property
    def ip(self) -> str:
        return self.__host.ip

    @property
    def target(self) -> NetDimmTargetEnum:
        return self.__host.target

    @target.setter
    def target(self, newval: NetDimmTargetEnum) -> None:
        self.__host.target = newval

    @property
    def version(self) -> NetDimmVersionEnum:
        return self.__host.version

    @version.setter
    def version(self, newval: NetDimmVersionEnum) -> None:
        self.__host.version = newval

    @property
    def filename(self) -> Optional[str]:
        with self.__lock:
            return self.__new_filename

    @filename.setter
    def filename(self, new_filename: Optional[str]) -> None:
        with self.__lock:
            self.__new_filename = new_filename

    @property
    def enabled(self) -> bool:
        with self.__lock:
            return self.__enabled

    @enabled.setter
    def enabled(self, enabled: bool) -> None:
        with self.__lock:
            self.__enabled = enabled

    @property
    def time_hack(self) -> bool:
        return self.__host.time_hack

    @time_hack.setter
    def time_hack(self, time_hack: bool) -> None:
        self.__host.time_hack = time_hack

    @property
    def send_timeout(self) -> Optional[int]:
        return self.__host.send_timeout

    @send_timeout.setter
    def send_timeout(self, send_timeout: Optional[int]) -> None:
        self.__host.send_timeout = send_timeout

    @property
    def outlet(self) -> Optional[Dict[str, object]]:
        if self.__outlet is None:
            return None
        return {
            'type': self.__outlet.type,
            **self.__outlet.serialize(),
        }

    @outlet.setter
    def outlet(self, outlet: Optional[Dict[str, object]]) -> None:
        self.__outlet = self.__spawn_outlet_interface(outlet)
        try:
            del self.__cache["power_state"]
        except KeyError:
            pass

    @property
    def power_state(self) -> CabinetPowerStateEnum:
        try:
            return cast(CabinetPowerStateEnum, self.__cache["power_state"])
        except KeyError:
            pass

        retval: CabinetPowerStateEnum
        if self.__outlet is None:
            retval = CabinetPowerStateEnum.POWER_DISABLED
        else:
            state = self.__outlet.getState()
            if state is None:
                retval = CabinetPowerStateEnum.POWER_UNKNOWN
            else:
                retval = CabinetPowerStateEnum.POWER_ON if state else CabinetPowerStateEnum.POWER_OFF
        self.__cache["power_state"] = retval
        return retval

    @power_state.setter
    def power_state(self, state: CabinetPowerStateEnum) -> None:
        try:
            del self.__cache["power_state"]
        except KeyError:
            pass

        if self.__outlet is None:
            return
        if state == CabinetPowerStateEnum.POWER_ON:
            self.__outlet.setState(True)
        elif state == CabinetPowerStateEnum.POWER_OFF:
            self.__outlet.setState(False)
            self.__host.alive = False

    @property
    def controllable(self) -> bool:
        return self.__outlet is not None and self.__controllable

    @controllable.setter
    def controllable(self, newvalue: bool) -> None:
        self.__controllable = newvalue

    def __print(self, string: str, newline: bool = True) -> None:
        if not self.quiet:
            log(string, newline=newline)

    def tick(self) -> None:
        """
        Tick the state machine forward.
        """

        with self.__lock:
            self.__host.tick()
            current_state = self.__state[0]

            # Startup state, only one transition to waiting for cabinet
            if current_state == CabinetStateEnum.STATE_STARTUP:
                if self.__enabled:
                    self.__print(f"Cabinet {self.ip} waiting for power on.")
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                return

            if not self.__enabled:
                self.__print(f"Cabinet {self.ip} has been disabled.")
                self.__state = (CabinetStateEnum.STATE_STARTUP, 0)
                return

            if self.power_state == CabinetPowerStateEnum.POWER_OFF:
                if current_state != CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON:
                    self.__print(f"Cabinet {self.ip} has been turned off.")
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                elif self.power_cycle:
                    # We need to check to see if we're recovering from a reboot sequence.
                    curtime = int(time.time())
                    waketime = self.__get_reboot_time()
                    if waketime is not None and curtime >= waketime:
                        # Time to wake up!
                        self.power_state = CabinetPowerStateEnum.POWER_ON
                        self.__set_reboot_time(None)
                        self.__print(f"Cabinet {self.ip} has finished power cycling, waiting for power on.")
                return

            # Wait for cabinet to power on state, transition to sending game
            # if the cabinet is active, transition to self if cabinet is not.
            if current_state == CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON:
                if self.__host.alive:
                    if self.__new_filename is None:
                        # Skip sending game, there's nothing to send
                        self.__print(f"Cabinet {self.ip} has no associated game, waiting for power off.")
                        self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF, 0)
                    else:
                        try:
                            info = self.__host.info()
                        except NetDimmException:
                            info = None

                        settings: Dict[SettingsEnum, bytes] = {}
                        eeprom = self.settings.get(self.__new_filename, None)
                        if eeprom is not None:
                            settings[SettingsEnum.SETTINGS_EEPROM] = eeprom
                        sram = self.srams.get(self.__new_filename, None)
                        if sram is not None:
                            with open(sram, "rb") as bfp:
                                settings[SettingsEnum.SETTINGS_SRAM] = bfp.read()

                        if info is not None and info.current_game_crc != 0:
                            # Its worth trying to CRC this game and seeing if it matches.
                            crc = self.__host.crc(self.__new_filename, self.patches.get(self.__new_filename, []), settings)
                            if crc == info.current_game_crc:
                                if info.game_crc_status == CRCStatusEnum.STATUS_VALID:
                                    self.__print(f"Cabinet {self.ip} is already running game {self.__new_filename}.")
                                    self.__current_filename = self.__new_filename
                                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF, 0)
                                    return
                                elif info.game_crc_status == CRCStatusEnum.STATUS_CHECKING:
                                    self.__print(f"Cabinet {self.ip} is already verifying game {self.__new_filename}.")
                                    self.__current_filename = self.__new_filename
                                    self.__state = (CabinetStateEnum.STATE_CHECK_CURRENT_GAME, 0)
                                    return

                        self.__print(f"Cabinet {self.ip} sending game {self.__new_filename}.")
                        self.__current_filename = self.__new_filename
                        self.__host.send(self.__new_filename, self.patches.get(self.__new_filename, []), settings)
                        self.__state = (CabinetStateEnum.STATE_SEND_CURRENT_GAME, 0)
                return

            # Wait for send to complete state. Transition to waiting for
            # cabinet power on if transfer failed. Stay in state if transfer
            # continuing. Transition to waiting for CRC verification if transfer
            # passes.
            if current_state == CabinetStateEnum.STATE_SEND_CURRENT_GAME:
                if self.__host.status == HostStatusEnum.STATUS_INACTIVE:
                    raise Exception("State error, shouldn't be possible!")
                elif self.__host.status == HostStatusEnum.STATUS_TRANSFERRING:
                    current, total = self.__host.progress
                    self.__state = (CabinetStateEnum.STATE_SEND_CURRENT_GAME, int(float(current * 100) / float(total)))
                elif self.__host.status == HostStatusEnum.STATUS_FAILED:
                    self.__print(f"Cabinet {self.ip} failed to send game, waiting for power on.")
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                elif self.__host.status == HostStatusEnum.STATUS_COMPLETED:
                    self.__print(f"Cabinet {self.ip} succeeded sending game, rebooting and verifying game CRC.")
                    self.__host.reboot()
                    self.__state = (CabinetStateEnum.STATE_CHECK_CURRENT_GAME, 0)
                return

            # Wait for the CRC verification screen to finish. Transition to waiting
            # for cabinet power off if CRC passes. Transition to waiting for power
            # on if CRC fails. If CRC is still in progress wait. If the cabinet
            # is turned off or the game is changed, also move back to waiting for
            # power on to send a new game.
            if current_state == CabinetStateEnum.STATE_CHECK_CURRENT_GAME:
                if not self.__host.alive:
                    self.__print(f"Cabinet {self.ip} turned off, waiting for power on.")
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                elif self.__current_filename != self.__new_filename:
                    if self.power_cycle:
                        self.__print(f"Cabinet {self.ip} changed game to {self.__new_filename}, waiting for power cycle.")
                        self.__host.wipe()

                        curtime = int(time.time())
                        self.__set_reboot_time(curtime + self.REBOOT_LENGTH)
                        self.power_state = CabinetPowerStateEnum.POWER_OFF
                    else:
                        self.__print(f"Cabinet {self.ip} changed game to {self.__new_filename}, waiting for power on.")

                    self.__current_filename = self.__new_filename
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                else:
                    try:
                        info = self.__host.info()
                    except NetDimmException:
                        info = None
                    if info is not None and info.current_game_crc != 0:
                        if info.game_crc_status == CRCStatusEnum.STATUS_VALID:
                            # Game passed onboard CRC, consider it running!
                            self.__print(f"Cabinet {self.ip} passed CRC verification for {self.__current_filename}, waiting for power off.")
                            self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF, 0)
                        elif info.game_crc_status == CRCStatusEnum.STATUS_DISABLED:
                            # Game onboard CRC screen was disabled, can't tell if the game is good or not! We could
                            # ignore this and just pretend the game was good, but it means that if the server was restarted
                            # while the cabinet was already running, it would have no way to synchronize with the state of
                            # the world. If that's what you really want, you should probably just be using "netdimm_send"
                            # instead of managing the cabinet through this class. So, resend the game if we hit this.
                            self.__print(f"Cabinet {self.ip} had CRC verification disabled for {self.__current_filename}, waiting for power on.")
                            self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                        elif info.game_crc_status in {CRCStatusEnum.STATUS_INVALID, CRCStatusEnum.STATUS_BAD_MEMORY}:
                            # Game failed onboard CRC, try sending again!
                            self.__print(f"Cabinet {self.ip} failed CRC verification for {self.__current_filename}, waiting for power on.")
                            self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                return

            # Wait for cabinet to turn off again. Transition to waiting for
            # power to come on if the cabinet is inactive. Transition to
            # waiting for power to come on if game changes. Stay in state
            # if cabinet stays on.
            if current_state == CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF:
                if not self.__host.alive:
                    self.__print(f"Cabinet {self.ip} turned off, waiting for power on.")
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                elif self.__current_filename != self.__new_filename:
                    if self.power_cycle:
                        self.__print(f"Cabinet {self.ip} changed game to {self.__new_filename}, waiting for power cycle.")
                        self.__host.wipe()

                        curtime = int(time.time())
                        self.__set_reboot_time(curtime + self.REBOOT_LENGTH)
                        self.power_state = CabinetPowerStateEnum.POWER_OFF
                    else:
                        self.__print(f"Cabinet {self.ip} changed game to {self.__new_filename}, waiting for power on.")
                    self.__current_filename = self.__new_filename
                    self.__state = (CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
                return

            raise Exception("State error, impossible state!")

    @property
    def state(self) -> Tuple[CabinetStateEnum, int]:
        """
        Returns the current state as a string, and the progress through that state
        as an integer, bounded between 0-100.
        """
        with self.__lock:
            if self.power_state == CabinetPowerStateEnum.POWER_OFF:
                waketime = self.__get_reboot_time()
                if waketime is None:
                    return (CabinetStateEnum.STATE_TURNED_OFF, 0)
                else:
                    return (CabinetStateEnum.STATE_POWER_CYCLE, 0)
            elif self.__enabled:
                return self.__state
            else:
                return (CabinetStateEnum.STATE_DISABLED, 0)

    def info(self) -> Optional[NetDimmInfo]:
        with self.__lock:
            if self.__enabled:
                return self.__host.info()
            else:
                return None


class EmptyObject:
    pass


empty = EmptyObject()


class CabinetManager:
    def __init__(self, cabinets: Sequence[Cabinet]) -> None:
        self.__cabinets: Dict[str, Cabinet] = {cab.ip: cab for cab in cabinets}
        self.__lock: threading.Lock = threading.Lock()
        self.__thread: threading.Thread = threading.Thread(target=self.__poll_thread)
        self.__thread.setDaemon(True)
        self.__thread.start()

    def __repr__(self) -> str:
        return f"CabinetManager([{', '.join(repr(cab) for cab in self.cabinets)}])"

    @staticmethod
    def from_yaml(yaml_file: str) -> "CabinetManager":
        with open(yaml_file, "r") as fp:
            data = yaml.safe_load(fp)

        if data is None:
            # Assume this is an empty file
            return CabinetManager([])

        if not isinstance(data, dict):
            raise CabinetException(f"Invalid YAML file format for {yaml_file}, missing list of cabinets!")

        cabinets: List[Cabinet] = []
        for ip, cab in data.items():
            try:
                ip = str(ipaddress.IPv4Address(ip))
            except ValueError:
                raise CabinetException("Invalid YAML file format for {yaml_file}, IP address {ip} is not valid!")

            if not isinstance(cab, dict):
                raise CabinetException(f"Invalid YAML file format for {yaml_file}, missing cabinet details for {ip}!")
            for key in ["description", "filename", "roms"]:
                if key not in cab:
                    raise CabinetException(f"Invalid YAML file format for {yaml_file}, missing {key} for {ip}!")
            if cab['filename'] is not None and not os.path.isfile(str(cab['filename'])):
                raise CabinetException(f"Invalid YAML file format for {yaml_file}, file {cab['filename']} for {ip} is not a file!")
            for rom, patches in cab['roms'].items():
                if not os.path.isfile(str(rom)):
                    raise CabinetException(f"Invalid YAML file format for {yaml_file}, file {rom} for {ip} is not a file!")
                for patch in patches:
                    if not os.path.isfile(str(patch)):
                        raise CabinetException(f"Invalid YAML file format for {yaml_file}, file {patch} for {ip} is not a file!")

            cabinet = Cabinet(
                ip=ip,
                description=str(cab['description']),
                region=CabinetRegionEnum(str(cab['region']).lower()),
                filename=str(cab['filename']) if cab['filename'] is not None else None,
                patches={str(rom): [str(p) for p in cab['roms'][rom]] for rom in cab['roms']},
                # This is accessed differently since we have older YAML files that might need upgrading.
                settings={str(rom): (bytes(data) if bool(data) else None) for (rom, data) in cab.get('settings', {}).items()},
                # This is accessed differently since we have older YAML files that might need upgrading.
                srams={str(rom): (str(data) if bool(data) else None) for (rom, data) in cab.get('srams', {}).items()},
                outlet=cab.get('outlet'),
                target=NetDimmTargetEnum(str(cab['target'])) if 'target' in cab else None,
                version=NetDimmVersionEnum(str(cab['version'])) if 'version' in cab else None,
                enabled=(True if 'disabled' not in cab else (not cab['disabled'])),
                controllable=(True if 'controllable' not in cab else bool(cab['controllable'])),
                power_cycle=(False if 'power_cycle' not in cab else bool(cab['power_cycle'])),
                time_hack=(False if 'time_hack' not in cab else bool(cab['time_hack'])),
                send_timeout=(None if 'send_timeout' not in cab else int(cab['send_timeout'])),
            )
            if cabinet.target == NetDimmTargetEnum.TARGET_NAOMI:
                # Make sure that the settings are correct for the EEPROM size.
                cabinet.settings = {
                    name: None if (settings is not None and len(settings) != NaomiSettingsPatcher.EEPROM_SIZE) else settings
                    for name, settings in cabinet.settings.items()
                }
            else:
                # Nothing can have settings outside of Naomi until we support it.
                cabinet.settings = {name: None for name in cabinet.settings}
                # Same goes for SRAM stuff.
                cabinet.srams = {name: None for name in cabinet.srams}
            cabinets.append(cabinet)

        return CabinetManager(cabinets)

    def to_yaml(self, yaml_file: str) -> None:
        data: Dict[str, Dict[str, Optional[Union[bool, str, Optional[int], Dict[str, List[str]], Dict[str, List[int]], Dict[str, Optional[str]], Dict[str, object]]]]] = {}

        with self.__lock:
            cabinets: List[Cabinet] = sorted([cab for _, cab in self.__cabinets.items()], key=lambda cab: cab.ip)

        for cab in cabinets:
            data[cab.ip] = {
                'description': cab.description,
                'region': cab.region.value,
                'target': cab.target.value,
                'version': cab.version.value,
                'filename': cab.filename,
                'time_hack': cab.time_hack,
                'roms': cab.patches,
                # Bytes isn't a serializable type, so serialize it as a list of ints. If the settings is
                # None for a ROM, serialize it as an empty list.
                'settings': {rom: [x for x in (settings or [])] for (rom, settings) in cab.settings.items()},
                'srams': cab.srams,
                'controllable': cab.controllable,
                'power_cycle': cab.power_cycle,
            }
            if not cab.enabled:
                data[cab.ip]['disabled'] = True
            if cab.send_timeout is not None:
                data[cab.ip]['send_timeout'] = cab.send_timeout
            if cab.outlet is not None:
                data[cab.ip]['outlet'] = cab.outlet

        with open(yaml_file, "w") as fp:
            yaml.dump(data, fp)

    def __poll_thread(self) -> None:
        while True:
            with self.__lock:
                cabinets: List[Cabinet] = [cab for _, cab in self.__cabinets.items()]

            for cabinet in cabinets:
                cabinet.tick()

            time.sleep(1)

    @property
    def cabinets(self) -> List[Cabinet]:
        with self.__lock:
            return sorted([cab for _, cab in self.__cabinets.items()], key=lambda cab: cab.ip)

    def cabinet(self, ip: str) -> Cabinet:
        with self.__lock:
            if ip not in self.__cabinets:
                raise CabinetException(f"There is no cabinet with the IP {ip}")
            return self.__cabinets[ip]

    def add_cabinet(self, cab: Cabinet) -> None:
        with self.__lock:
            if cab.ip in self.__cabinets:
                raise CabinetException(f"There is already a cabinet with the IP {cab.ip}")
            self.__cabinets[cab.ip] = cab

    def remove_cabinet(self, ip: str) -> None:
        with self.__lock:
            if ip not in self.__cabinets:
                raise CabinetException(f"There is no cabinet with the IP {ip}")
            del self.__cabinets[ip]

    def update_cabinet(
        self,
        ip: str,
        *,
        region: Optional[CabinetRegionEnum] = None,
        description: Optional[str] = None,
        filename: Union[Optional[str], EmptyObject] = empty,
        patches: Optional[Dict[str, Sequence[str]]] = None,
        settings: Optional[Dict[str, Optional[bytes]]] = None,
        srams: Optional[Dict[str, Optional[str]]] = None,
        outlet: Union[Optional[Dict[str, object]], EmptyObject] = empty,
        target: Union[Optional[NetDimmTargetEnum], EmptyObject] = empty,
        version: Union[Optional[NetDimmVersionEnum], EmptyObject] = empty,
        send_timeout: Union[Optional[int], EmptyObject] = empty,
        time_hack: Optional[bool] = None,
        controllable: Optional[bool] = None,
        power_cycle: Optional[bool] = None,
        enabled: Optional[bool] = None,
    ) -> None:
        with self.__lock:
            if ip not in self.__cabinets:
                raise CabinetException(f"There is no cabinet with the IP {ip}")
            # Make sure we don't reboot the cabinet if we update settings.
            existing_cab = self.__cabinets[ip]
            if description is not None:
                existing_cab.description = description
            if not isinstance(target, EmptyObject):
                existing_cab.target = target or NetDimmTargetEnum.TARGET_NAOMI
            if region is not None:
                existing_cab.region = region
            if not isinstance(version, EmptyObject):
                existing_cab.version = version or NetDimmVersionEnum.VERSION_4_01
            if patches is not None:
                existing_cab.patches = {rom: [p for p in patches[rom]] for rom in patches}
            if settings is not None:
                existing_cab.settings = {rom: settings[rom] for rom in settings}
            if srams is not None:
                existing_cab.srams = {rom: srams[rom] for rom in srams}
            if not isinstance(outlet, EmptyObject):
                existing_cab.outlet = outlet
            if not isinstance(filename, EmptyObject):
                existing_cab.filename = filename
            if enabled is not None:
                existing_cab.enabled = enabled
            if time_hack is not None:
                existing_cab.time_hack = time_hack
            if controllable is not None:
                existing_cab.controllable = controllable
            if power_cycle is not None:
                existing_cab.power_cycle = power_cycle
            if not isinstance(send_timeout, EmptyObject):
                existing_cab.send_timeout = send_timeout

    def cabinet_exists(self, ip: str) -> bool:
        with self.__lock:
            return ip in self.__cabinets
