import multiprocessing
import os
import queue
import subprocess
from typing import Optional, Tuple, TYPE_CHECKING

from netboot.netboot import NetDimm, NetDimmException


if TYPE_CHECKING:
    from typing import Any  # noqa


def _send_file_to_host(host: str, filename: str, target: str, version: str, progress_queue: "multiprocessing.Queue[Tuple[str, Any]]") -> None:
    def capture_progress(sent: int, total: int) -> None:
        progress_queue.put(("progress", (sent, total)))

    try:
        netdimm = NetDimm(host, target=target, version=version, quiet=True)
        with open(filename, "rb") as fp:
            netdimm.send(fp.read(), progress_callback=capture_progress)

        progress_queue.put(("success", None))
    except Exception as e:
        progress_queue.put(("failure", str(e)))


class HostException(Exception):
    pass


class Host:
    STATUS_INACTIVE = "inactive"
    STATUS_TRANSFERRING = "transferring"
    STATUS_COMPLETED = "completed"
    STATUS_FAILED = "failed"

    def __init__(self, ip: str, target: Optional[str] = None, version: Optional[str] = None) -> None:
        self.ip: str = ip
        self.queue: "multiprocessing.Queue[Tuple[str, Any]]" = multiprocessing.Queue()
        self.proc: Optional[multiprocessing.Process] = None
        self.lastprogress: Tuple[int, int] = (-1, -1)
        self.laststatus: Optional[str] = None

        if target is not None and target not in [NetDimm.TARGET_CHIHIRO, NetDimm.TARGET_NAOMI, NetDimm.TARGET_TRIFORCE]:
            raise NetDimmException(f"Invalid target platform {target}")
        self.target: str = target or NetDimm.TARGET_NAOMI
        if version is not None and version not in [NetDimm.NETDIMM_VERSION_1_07, NetDimm.NETDIMM_VERSION_2_03, NetDimm.NETDIMM_VERSION_2_15, NetDimm.NETDIMM_VERSION_3_01]:
            raise NetDimmException(f"Invalid NetDimm version {version}")
        self.version: str = version or NetDimm.NETDIMM_VERSION_3_01

    @property
    def alive(self) -> bool:
        """
        Given a host, returns True if the host is alive, or False if the
        host is not replying to ping.
        """

        with open(os.devnull, 'w') as DEVNULL:
            try:
                subprocess.check_call(["ping", "-c1", "-W1", self.ip], stdout=DEVNULL, stderr=DEVNULL)
                return True
            except subprocess.CalledProcessError:
                return False

    def reboot(self) -> bool:
        """
        Given a host, attempt to reboot it. Returns True if succeeded or
        False if failed.
        """
        netdimm = NetDimm(self.ip, target=self.target, version=self.version, quiet=True)
        try:
            netdimm.reboot()
            return True
        except NetDimmException:
            return False

    @property
    def status(self) -> str:
        """
        Given a host, returns the status of any active transfer.
        """
        self.__update_progress()

        if self.laststatus is not None:
            # If we have a status, that's the current deal
            return self.laststatus
        if self.proc is None:
            # No proc means no current transfer
            return self.STATUS_INACTIVE
        # If we got here, we have a proc and no status, so we're transferring
        return self.STATUS_TRANSFERRING

    @property
    def progress(self) -> Tuple[int, int]:
        """
        Given a host, returns the current progress (amount sent, total)
        of any active transfer. If a transfer is not active, throws.
        """
        if self.proc is None or self.laststatus is not None:
            raise HostException("There is no active transfer")
        return self.lastprogress

    def __update_progress(self) -> None:
        if self.proc is None:
            # Nothing to update here
            return

        while True:
            try:
                update = self.queue.get_nowait()
            except queue.Empty:
                # No more updates
                return

            # Normal progress update
            if update[0] == "progress":
                self.lastprogress = (update[1][0], update[1][1])
                continue

            # Transfer finished, so we should update our final status and wait on the process
            if update[0] == "success":
                self.laststatus = self.STATUS_COMPLETED
            elif update[0] == "failure":
                self.laststatus = self.STATUS_FAILED

            self.proc.join()
            self.proc = None
            return

    def send(self, filename: str) -> None:
        if self.proc is not None:
            raise HostException("Host has active transfer already")
        self.lastprogress = (-1, -1)
        self.laststatus = None

        # Start the send
        self.proc = multiprocessing.Process(target=_send_file_to_host, args=(self.ip, filename, self.target, self.version, self.queue))
        self.proc.start()

        # Don't yield control back until we have got the first response from the process
        while self.lastprogress == (-1, -1) and self.proc is not None:
            self.__update_progress()
