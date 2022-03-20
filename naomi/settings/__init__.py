from naomi.settings.settings import (
    NaomiSettingsWrapper,
    NaomiSettingsManager,
    get_default_settings_directory,
)
from settings.settings import (
    SettingsParseException,
    SettingsSaveException,
    JSONParseException,
    ReadOnlyCondition,
    DefaultCondition,
    DefaultConditionGroup,
    Setting,
    Settings,
    SettingsConfig,
)

try:
    from naomi.settings.editor import NaomiSettingsEditor
except ModuleNotFoundError:
    # DragonCurses not installed, provide a dummy stub.
    class NaomiSettingsEditor:  # type: ignore
        def __init__(self, settings: NaomiSettingsWrapper, enable_unicode: bool = True) -> None:
            self.settings = settings

        def run(self) -> bool:
            return False


__all__ = [
    "NaomiSettingsWrapper",
    "NaomiSettingsManager",
    "NaomiSettingsEditor",
    "get_default_settings_directory",
    "SettingsParseException",
    "SettingsSaveException",
    "JSONParseException",
    "ReadOnlyCondition",
    "DefaultCondition",
    "DefaultConditionGroup",
    "Setting",
    "Settings",
    "SettingsConfig",
]
