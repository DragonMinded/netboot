from .interface import OutletInterface
from .snmp import SNMPOutlet
from .ap7900 import AP7900Outlet
from .np_02b import NP02BOutlet

from typing import List, Type


ALL_OUTLET_CLASSES: List[Type[OutletInterface]] = [SNMPOutlet, AP7900Outlet, NP02BOutlet]

__all__ = [
    "OutletInterface",
    "SNMPOutlet",
    "AP7900Outlet",
    "NP02BOutlet",
    "ALL_OUTLET_CLASSES",
]
