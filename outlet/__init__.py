from .interface import OutletInterface
from .snmp import SNMPOutlet
from .ap7900 import AP7900Outlet

from typing import List, Type


ALL_OUTLET_CLASSES: List[Type[OutletInterface]] = [SNMPOutlet, AP7900Outlet]

__all__ = [
    "OutletInterface",
    "SNMPOutlet",
    "AP7900Outlet",
    "ALL_OUTLET_CLASSES",
]
