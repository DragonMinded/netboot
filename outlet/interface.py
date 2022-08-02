from abc import ABC, abstractmethod
from typing import Optional


class OutletInterface(ABC):
    @abstractmethod
    def getState(self) -> Optional[bool]:
        ...

    @abstractmethod
    def setState(self, state: bool) -> None:
        ...
