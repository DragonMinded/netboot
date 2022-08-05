import multiprocessing
import multiprocessing.synchronize
import os
import platform
import psutil  # type: ignore
import queue
import subprocess
import sys
import threading
import time
from enum import Enum
from typing import Any, Dict, Optional, Sequence, Tuple, Union, overload

from arcadeutils import FileBytes, BinaryDiff
from netboot.log import log
from netdimm import NetDimm, NetDimmInfo, NetDimmException, NetDimmVersionEnum, NetDimmTargetEnum
from naomi import NaomiSettingsPatcher, get_default_trojan as get_default_naomi_trojan


class SettingsEnum(Enum):
    SETTINGS_EEPROM = "eeprom"
    SETTINGS_SRAM = "sram"


@overload
def _handle_patches(data: bytes, target: NetDimmTargetEnum, patches: Sequence[str], settings: Dict[SettingsEnum, bytes]) -> bytes:
    ...


@overload
def _handle_patches(data: FileBytes, target: NetDimmTargetEnum, patches: Sequence[str], settings: Dict[SettingsEnum, bytes]) -> FileBytes:
    ...


def _handle_patches(data: Union[bytes, FileBytes], target: NetDimmTargetEnum, patches: Sequence[str], settings: Dict[SettingsEnum, bytes]) -> Union[bytes, FileBytes]:
    # Patch it
    for patch in patches:
        with open(patch, "r") as pp:
            differences = pp.readlines()
        differences = [d.strip() for d in differences if d.strip()]
        data = BinaryDiff.patch(data, differences)

    for typ, setting in settings.items():
        if typ == SettingsEnum.SETTINGS_EEPROM:
            # Attach any settings file requested.
            if target == NetDimmTargetEnum.TARGET_NAOMI:
                patcher = NaomiSettingsPatcher(data, get_default_naomi_trojan())
                patcher.put_eeprom(setting)
                data = patcher.data
        elif typ == SettingsEnum.SETTINGS_SRAM:
            # Attach any settings file requested.
            if target == NetDimmTargetEnum.TARGET_NAOMI:
                patcher = NaomiSettingsPatcher(data, get_default_naomi_trojan())
                patcher.put_sram(setting)
                data = patcher.data

    return data


def _send_file_to_host(
    host: str,
    filename: str,
    patches: Sequence[str],
    settings: Dict[SettingsEnum, bytes],
    target: NetDimmTargetEnum,
    version: NetDimmVersionEnum,
    timeout: Optional[int],
    parent_pid: int,
    progress_queue: "multiprocessing.Queue[Tuple[str, Any]]",
) -> None:
    def capture_progress(sent: int, total: int) -> None:
        # See if we need to bail out since our parent disappeared
        if not psutil.pid_exists(parent_pid):
            sys.exit(1)
        progress_queue.put(("progress", (sent, total)))

    try:
        netdimm = NetDimm(host, version=version, timeout=timeout)

        # Grab the image itself
        with open(filename, "rb") as fp:
            # Get a memory-based file representation so we don't load
            # too much data into RAM at once.
            data = FileBytes(fp)

            # Patch it
            data = _handle_patches(data, target, patches, settings)

            # Send it
            netdimm.send(data, progress_callback=capture_progress)

        progress_queue.put(("success", None))
    except Exception as e:
        progress_queue.put(("failure", str(e)))


class HostException(Exception):
    pass


class HostStatusEnum(Enum):
    STATUS_INACTIVE = "inactive"
    STATUS_TRANSFERRING = "transferring"
    STATUS_COMPLETED = "completed"
    STATUS_FAILED = "failed"


class Host:
    DEBOUNCE_SECONDS = 3

    def __init__(
        self,
        ip: str,
        target: Optional[NetDimmTargetEnum] = None,
        version: Optional[NetDimmVersionEnum] = None,
        send_timeout: Optional[int] = None,
        time_hack: bool = False,
        quiet: bool = False,
    ) -> None:
        self.target: NetDimmTargetEnum = target or NetDimmTargetEnum.TARGET_NAOMI
        self.version: NetDimmVersionEnum = version or NetDimmVersionEnum.VERSION_4_01
        self.ip: str = ip
        self.alive: bool = False
        self.quiet: bool = quiet
        self.time_hack: bool = time_hack
        self.send_timeout: Optional[int] = send_timeout
        self.__queue: "multiprocessing.Queue[Tuple[str, Any]]" = multiprocessing.Queue()
        self.__lock: multiprocessing.synchronize.Lock = multiprocessing.Lock()
        self.__proc: Optional[multiprocessing.Process] = None
        self.__lastprogress: Tuple[int, int] = (-1, -1)
        self.__laststatus: Optional[HostStatusEnum] = None
        self.__thread: threading.Thread = threading.Thread(target=self.__poll_thread)
        self.__thread.setDaemon(True)
        self.__thread.start()

    def __repr__(self) -> str:
        return f"Host(ip={repr(self.ip)}, target={repr(self.target)}, version={repr(self.version)}, send_timeout={repr(self.send_timeout)}, time_hack={repr(self.time_hack)})"

    def __print(self, string: str, newline: bool = True) -> None:
        if not self.quiet:
            log(string, newline=newline)

    def __poll_thread(self) -> None:
        success_count: int = 0
        failure_count: int = 0
        on_windows: bool = platform.system() == "Windows"
        last_timehack: float = time.time()

        while True:
            # Dont bother if we're actively sending
            if self.status != HostStatusEnum.STATUS_TRANSFERRING:
                with open(os.devnull, 'w') as DEVNULL:
                    try:
                        if on_windows:
                            call = ["ping", "-n", "1", "-w", "1", self.ip]
                        else:
                            call = ["ping", "-c1", "-W1", self.ip]
                        subprocess.check_call(call, stdout=DEVNULL, stderr=DEVNULL)
                        alive = True
                    except subprocess.CalledProcessError:
                        alive = False

                # Only claim up if it response to a number of pings.
                if alive:
                    success_count += 1
                    failure_count = 0
                    if success_count >= self.DEBOUNCE_SECONDS:
                        with self.__lock:
                            if self.alive != alive:
                                self.__print(f"Host {self.ip} started responding to ping, marking up.")
                            self.alive = True

                    # Perform the time hack if so requested.
                    current = time.time()
                    if (current - last_timehack) >= 5.0:
                        last_timehack = current
                        with self.__lock:
                            if self.time_hack:
                                netdimm = NetDimm(self.ip, version=self.version, timeout=5)
                                try:
                                    netdimm.set_time_limit(10)
                                    self.__print(f"Host {self.ip} reset time limit with time hack.")
                                except NetDimmException:
                                    pass
                else:
                    success_count = 0
                    failure_count += 1
                    if failure_count >= self.DEBOUNCE_SECONDS:
                        with self.__lock:
                            if self.alive != alive:
                                self.__print(f"Host {self.ip} stopped responding to ping, marking down.")
                            self.alive = False

            time.sleep(2 if success_count >= self.DEBOUNCE_SECONDS else 1)

    def reboot(self) -> bool:
        """
        Given a host, attempt to reboot it. Returns True if succeeded or
        False if failed.
        """
        with self.__lock:
            if self.__proc is not None:
                raise HostException("Cannot reboot host mid-transfer.")

            netdimm = NetDimm(self.ip, version=self.version, timeout=5)
            try:
                netdimm.reboot()
                return True
            except NetDimmException:
                return False

    def tick(self) -> None:
        """
        Tick the host mechanism forward. When transferring, this will update
        the status and progress.
        """
        with self.__lock:
            self.__update_progress()

    @property
    def status(self) -> HostStatusEnum:
        """
        Given a host, returns the status of any active transfer.
        """
        with self.__lock:
            if self.__laststatus is not None:
                # If we have a status, that's the current deal
                return self.__laststatus
            if self.__proc is None:
                # No proc means no current transfer
                return HostStatusEnum.STATUS_INACTIVE
            # If we got here, we have a proc and no status, so we're transferring
            return HostStatusEnum.STATUS_TRANSFERRING

    @property
    def progress(self) -> Tuple[int, int]:
        """
        Given a host, returns the current progress (amount sent, total)
        of any active transfer. If a transfer is not active, throws.
        Note that you should check the result of status before calling
        progress, so you know whether or not an exception will be raised.
        """

        with self.__lock:
            if self.__lastprogress == (-1, -1):
                raise HostException("There is no active transfer")
            return self.__lastprogress

    def __update_progress(self) -> None:
        """
        Update progress if needed, with respect to a separate send process. Note
        that this should only be called by something that has a lock.
        """

        if self.__proc is None:
            # Nothing to update here
            return

        while True:
            try:
                update = self.__queue.get_nowait()
            except queue.Empty:
                # No more updates
                return

            # Normal progress update
            if update[0] == "progress":
                self.__lastprogress = (update[1][0], update[1][1])
                continue

            # Transfer finished, so we should update our final status and wait on the process
            if update[0] == "success":
                self.__print(f"Host {self.ip} succeeded in sending image.")
                self.__laststatus = HostStatusEnum.STATUS_COMPLETED
            elif update[0] == "failure":
                self.__print(f"Host {self.ip} failed to send image: {update[1]}.")
                self.__laststatus = HostStatusEnum.STATUS_FAILED
            self.__lastprogress = (-1, -1)

            self.__proc.join()
            self.__proc = None
            return

    def send(self, filename: str, patches: Sequence[str], settings: Dict[SettingsEnum, bytes]) -> None:
        with self.__lock:
            if self.__proc is not None:
                raise HostException("Host has active transfer already")
            self.__lastprogress = (-1, -1)
            self.__laststatus = None
            self.__print(f"Host {self.ip} started sending image.")

            # Start the send
            self.__proc = multiprocessing.Process(
                target=_send_file_to_host,
                args=(self.ip, filename, patches, settings, self.target, self.version, self.send_timeout, os.getpid(), self.__queue),
            )
            self.__proc.start()

            # Don't yield control back until we have got the first response from the process
            while self.__lastprogress == (-1, -1) and self.__proc is not None:
                self.__update_progress()

    def crc(self, filename: str, patches: Sequence[str], settings: Dict[SettingsEnum, bytes]) -> int:
        # Grab the image itself
        with open(filename, "rb") as fp:
            data = FileBytes(fp)

            # Patch it
            data = _handle_patches(data, self.target, patches, settings)

            # Now, apply the CRC algorithm over it.
            return NetDimm.crc(data)

    def wipe(self) -> None:
        with self.__lock:
            if self.__proc is not None:
                # Host is actively transferring, can't do anything.
                return

            try:
                netdimm = NetDimm(self.ip, version=self.version, timeout=5)
                netdimm.wipe_current_game()
            except NetDimmException:
                pass

    def info(self) -> Optional[NetDimmInfo]:
        with self.__lock:
            if self.__proc is not None:
                # Host is actively transferring, don't bother requesting info.
                return None

            try:
                netdimm = NetDimm(self.ip, version=self.version, timeout=5)
                return netdimm.info()
            except NetDimmException:
                return None
