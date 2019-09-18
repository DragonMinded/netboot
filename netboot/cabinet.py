from typing import Tuple

from netboot.hostutils import Host


class Cabinet:
    STATE_STARTUP = "startup"
    STATE_WAIT_FOR_CABINET_POWER_ON = "wait_power_on"
    STATE_SEND_CURRENT_GAME = "send_game"
    STATE_WAIT_FOR_CABINET_POWER_OFF = "wait_power_off"

    def __init__(self, ip: str, filename: str) -> None:
        self.host: Host = Host(ip)
        self.__current_filename: str = filename
        self.__new_filename: str = filename
        self.__state: Tuple[str, int] = (self.STATE_STARTUP, 0)

    @property
    def filename(self) -> str:
        return self.__new_filename

    @filename.setter
    def filename(self, new_filename: str) -> None:
        self.__new_filename = new_filename

    def tick(self) -> None:
        """
        Tick the state machine forward.
        """
        self.host.tick()
        current_state = self.__state[0]

        # Startup state, only one transition to waiting for cabinet
        if current_state == self.STATE_STARTUP:
            self.__state = (self.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
            return

        # Wait for cabinet to power on state, transition to sending game
        # if the cabinet is active, transition to self if cabinet is not.
        if current_state == self.STATE_WAIT_FOR_CABINET_POWER_ON:
            if self.host.alive:
                self.host.send(self.__new_filename)
                self.__state = (self.STATE_SEND_CURRENT_GAME, 0)
            return

        # Wait for send to complete state. Transition to waiting for
        # cabinet power on if transfer failed. Stay in state if transfer
        # continuing. Transition to waint for power off if transfer success.
        if current_state == self.STATE_SEND_CURRENT_GAME:
            if self.host.status == Host.STATUS_INACTIVE:
                raise Exception("State error, shouldn't be possible!")
            elif self.host.status == Host.STATUS_TRANSFERRING:
                current, total = self.host.progress
                self.__state = (self.STATE_SEND_CURRENT_GAME, int(float(current * 100) / float(total)))
            elif self.host.status == Host.STATUS_FAILED:
                self.__state = (self.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
            elif self.host.status == Host.STATUS_COMPLETED:
                self.host.reboot()
                self.__state = (self.STATE_WAIT_FOR_CABINET_POWER_OFF, 0)
            return

        # Wait for cabinet to turn off again. Transition to waiting for
        # power to come on if the cabinet is inactive. Transition to
        # waiting for power to come on if game changes. Stay in state
        # if cabinet stays on.
        if current_state == self.STATE_WAIT_FOR_CABINET_POWER_OFF:
            if not self.host.alive:
                self.__state = (self.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
            elif self.__current_filename != self.__new_filename:
                self.__current_filename = self.__new_filename
                self.__state = (self.STATE_WAIT_FOR_CABINET_POWER_ON, 0)
            return

        raise Exception("State error, impossible state!")

    @property
    def state(self) -> Tuple[str, int]:
        """
        Returns the current state as a string, and the progress through that state
        as an integer, bounded between 0-100.
        """
        return self.__state
