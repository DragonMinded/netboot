from abc import ABC, abstractmethod
from typing import ClassVar, Dict, Optional


class OutletInterface(ABC):
    type: ClassVar[str]

    @abstractmethod
    def serialize(self) -> Dict[str, object]:
        ...

    @abstractmethod
    def getState(self) -> Optional[bool]:
        ...

    @abstractmethod
    def setState(self, state: bool) -> None:
        ...
