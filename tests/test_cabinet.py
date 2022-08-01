import unittest
from typing import List, Optional, Tuple
from unittest.mock import MagicMock, patch

# We import internal stuff here since we don't want to test the public
# interfaces.
from netboot.cabinet import Cabinet, CabinetStateEnum, CabinetRegionEnum
from netboot.hostutils import HostStatusEnum
from netdimm import NetDimmInfo, CRCStatusEnum, NetDimmVersionEnum


class TestCabinet(unittest.TestCase):
    def spawn_cabinet(
        self,
        state: Optional[CabinetStateEnum] = None,
        filename: Optional[str] = None,
    ) -> Tuple[Cabinet, MagicMock]:
        cabinet = Cabinet(
            ip="1.2.3.4",
            region=CabinetRegionEnum.REGION_USA,
            description="test",
            filename=filename,
            patches={},
            settings={},
            srams={},
        )
        host = MagicMock()
        host.ip = "1.2.3.4"
        cabinet._Cabinet__host = host  # type: ignore
        if state is not None:
            cabinet._Cabinet__state = (state, 0)  # type: ignore
        return (cabinet, host)

    def test_state_initial(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, _ = self.spawn_cabinet()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_STARTUP)

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 waiting for power on."], logs)

    def test_state_host_dead_no_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            host.alive = False

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual([], logs)

    def test_state_host_alive_no_game_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            host.alive = True

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF)
            self.assertEqual(["Cabinet 1.2.3.4 has no associated game, waiting for power off."], logs)

    def test_state_host_alive_game_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON,
                filename="abc.bin",
            )
            host.alive = True

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_SEND_CURRENT_GAME)
            self.assertEqual(["Cabinet 1.2.3.4 sending game abc.bin."], logs)
            host.send.assert_called_with("abc.bin", [], {})

    def test_state_host_alive_already_running_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_VALID,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )
            host.crc = MagicMock(
                return_value=12345678
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF)
            self.assertEqual(["Cabinet 1.2.3.4 is already running game abc.bin."], logs)
            host.send.assert_not_called()

    def test_state_host_alive_already_checking_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_CHECKING,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )
            host.crc = MagicMock(
                return_value=12345678
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_CHECK_CURRENT_GAME)
            self.assertEqual(["Cabinet 1.2.3.4 is already verifying game abc.bin."], logs)
            host.send.assert_not_called()

    def test_state_host_alive_bad_crc_game_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_INVALID,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )
            host.crc = MagicMock(
                return_value=12345678
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_SEND_CURRENT_GAME)
            self.assertEqual(["Cabinet 1.2.3.4 sending game abc.bin."], logs)
            host.send.assert_called_with("abc.bin", [], {})

    def test_state_host_alive_different_crc_game_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=23456789,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_VALID,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )
            host.crc = MagicMock(
                return_value=12345678
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_SEND_CURRENT_GAME)
            self.assertEqual(["Cabinet 1.2.3.4 sending game abc.bin."], logs)
            host.send.assert_called_with("abc.bin", [], {})

    def test_state_host_sending_no_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_SEND_CURRENT_GAME,
                filename="abc.bin",
            )
            host.status = HostStatusEnum.STATUS_TRANSFERRING
            host.progress = (1, 2)

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_SEND_CURRENT_GAME)
            self.assertEqual(cabinet.state[1], 50)
            self.assertEqual([], logs)

    def test_state_host_sending_failed_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_SEND_CURRENT_GAME,
                filename="abc.bin",
            )
            host.status = HostStatusEnum.STATUS_FAILED

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 failed to send game, waiting for power on."], logs)

    def test_state_host_sending_succeeded_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_SEND_CURRENT_GAME,
                filename="abc.bin",
            )
            host.status = HostStatusEnum.STATUS_COMPLETED

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_CHECK_CURRENT_GAME)
            self.assertEqual(["Cabinet 1.2.3.4 succeeded sending game, rebooting and verifying game CRC."], logs)

    def test_state_host_checking_died_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = False

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 turned off, waiting for power on."], logs)

    def test_state_host_checking_new_game_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True
            cabinet.filename = "xyz.bin"

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 changed game to xyz.bin, waiting for power on."], logs)

    def test_state_host_checking_no_info_no_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_CHECK_CURRENT_GAME)
            self.assertEqual([], logs)

    def test_state_host_checking_crc_valid_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_VALID,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF)
            self.assertEqual(["Cabinet 1.2.3.4 passed CRC verification for abc.bin, waiting for power off."], logs)

    def test_state_host_checking_crc_invalid_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_INVALID,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 failed CRC verification for abc.bin, waiting for power on."], logs)

    def test_state_host_checking_crc_running_no_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_CHECKING,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_CHECK_CURRENT_GAME)
            self.assertEqual([], logs)

    def test_state_host_checking_crc_disabled_transition(self) -> None:
        logs: List[str] = []
        with patch('netboot.cabinet.log', new_callable=lambda: lambda log, newline: logs.append(log)):
            cabinet, host = self.spawn_cabinet(
                state=CabinetStateEnum.STATE_CHECK_CURRENT_GAME,
                filename="abc.bin",
            )
            host.alive = True
            host.info = MagicMock(
                return_value=NetDimmInfo(
                    current_game_crc=12345678,
                    current_game_size=5555,
                    game_crc_status=CRCStatusEnum.STATUS_DISABLED,
                    memory_size=5555,
                    firmware_version=NetDimmVersionEnum.VERSION_UNKNOWN,
                    available_game_memory=5555,
                    control_address=5555,
                ),
            )

            cabinet.tick()
            self.assertEqual(cabinet.state[0], CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON)
            self.assertEqual(["Cabinet 1.2.3.4 had CRC verification disabled for abc.bin, waiting for power on."], logs)
