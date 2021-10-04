from settings.settings import (
    SettingsParseException,
    SettingsSaveException,
    JSONParseException,
    ReadOnlyCondition,
    DefaultCondition,
    DefaultConditionGroup,
    Setting,
    Settings,
    SettingsWrapper,
    SettingsManager,
)
try:
    from settings.editor import SettingsEditor
except ModuleNotFoundError:
    # DragonCurses not installed, provide a dummy stub.
    class SettingsEditor:
        def __init__(self, settings: SettingsWrapper, enable_unicode: bool = True) -> None:
            self.settings = settings

    def run(self) -> bool:
        return False


__all__ = [
    "SettingsParseException",
    "SettingsSaveException",
    "JSONParseException",
    "ReadOnlyCondition",
    "DefaultCondition",
    "DefaultConditionGroup",
    "Setting",
    "Settings",
    "SettingsWrapper",
    "SettingsManager",
    "SettingsEditor",
]
