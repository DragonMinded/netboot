#! /usr/bin/env python3
import copy
import os
from dragoncurses.component import (
    Component,
    DeferredInput,
    LabelComponent,
    DialogBoxComponent,
    StickyComponent,
    ClickableComponent,
    SelectInputComponent,
    ButtonComponent,
    BorderComponent,
    ListComponent,
)
from dragoncurses.context import RenderContext, BoundingRectangle
from dragoncurses.scene import Scene
from dragoncurses.loop import execute
from dragoncurses.input import (
    InputEvent,
    KeyboardInputEvent,
    MouseInputEvent,
    Buttons,
    Keys,
)
from dragoncurses.settings import Settings as DragonCursesSettings
from typing import Any, Callable, Dict, List, Tuple, Union

from naomi.settings.settings import SettingsWrapper, Settings, ReadOnlyCondition


class ClickableSelectInputComponent(ClickableComponent, SelectInputComponent):
    def __repr__(self) -> str:
        return "ClickableSelectInputComponent(selected={}, options={}, focused={})".format(repr(self.selected), repr(self.options), "True" if self.focus else "False")


class TabComponent(Component):

    def __init__(self, tabs: List[Tuple[str, Component]]) -> None:
        super().__init__()
        self.__buttons = [
            ButtonComponent(
                name,
                formatted=True,
                centered=True,
            ).on_click(lambda component, button: self.__change_tab(component))
            for name, _ in tabs
        ]
        self.__borders = [
            BorderComponent(
                component,
                style=(
                    BorderComponent.SINGLE if DragonCursesSettings.enable_unicode
                    else BorderComponent.ASCII
                ),
            )
            for _, component in tabs
        ]
        self.__tabs = tabs
        self.__selected = 0
        self.__drawn = False
        self.__highlight()

    def __highlight(self) -> None:
        for i, button in enumerate(self.__buttons):
            button.invert = i == self.__selected

    def __change_tab(self, component: Component) -> bool:
        for i, btn in enumerate(self.__buttons):
            if btn is component:
                self.__selected = i
                self.__drawn = False
                self.__highlight()

                return True
        return False

    @property
    def dirty(self) -> bool:
        if not self.__drawn:
            return True
        for component in [*self.__buttons, *self.__borders]:
            if component.dirty:
                return True
        return False

    def attach(self, scene: "Scene", settings: Dict[str, Any]) -> None:
        for component in [*self.__buttons, *self.__borders]:
            component._attach(scene, settings)

    def detach(self) -> None:
        for component in [*self.__buttons, *self.__borders]:
            component._detach()

    def tick(self) -> None:
        for component in [*self.__buttons, *self.__borders]:
            component.tick()

    def handle_input(self, event: "InputEvent") -> Union[bool, DeferredInput]:
        if isinstance(event, KeyboardInputEvent):
            if event.character == Keys.TAB:
                self.__selected = (self.__selected + 1) % len(self.__buttons)
                self.__drawn = False
                self.__highlight()

                return True

        for component in [*self.__buttons, self.__borders[self.__selected]]:
            if component._handle_input(event):
                return True
        return False

    def render(self, context: RenderContext) -> None:
        # Bookkeeping please!
        self.__drawn = True
        context.clear()

        # First, draw the tab buttons.
        for i, button in enumerate(self.__buttons):
            button._render(
                context,
                BoundingRectangle(
                    top=context.bounds.top,
                    bottom=context.bounds.top + 3,
                    left=context.bounds.left + (22 * i),
                    right=context.bounds.left + (22 * i) + 21,
                ),
            )

        # Now, draw the actual component that is selected.
        self.__borders[self.__selected]._render(
            context,
            BoundingRectangle(
                top=context.bounds.top + 3,
                bottom=context.bounds.bottom,
                left=context.bounds.left,
                right=context.bounds.right,
            ),
        )


class SettingsComponent(Component):
    def __init__(self, serial: str, settings: Settings) -> None:
        super().__init__()
        self.__all_settings = settings.settings
        self.__settings = [s for s in settings.settings if s.read_only is not True]
        self.__container: Component
        if settings.settings:
            if self.__settings:
                self.__labels = [
                    LabelComponent(setting.name)
                    for setting in self.__settings
                ]
                self.__inputs = [
                    ClickableSelectInputComponent(
                        setting.values[setting.current or list(setting.values.keys())[0]],
                        [v for _, v in setting.values.items()],
                        focused=False
                    ).on_click(self.__click_select)
                    for setting in self.__settings
                ]
                self.__inputs[0].focus = True
                self.__calculate_visible()
                self.__container = ListComponent(
                    [
                        ListComponent(
                            list(pair),
                            direction=ListComponent.DIRECTION_LEFT_TO_RIGHT,
                        )
                        for pair in zip(self.__labels, self.__inputs)
                    ],
                    direction=ListComponent.DIRECTION_TOP_TO_BOTTOM,
                    size=2,
                )
            else:
                self.__container = LabelComponent(
                    f"Settings definition file \"{serial}.settings\" specifies no editable settings.\n"
                    "As a result, we cannot display or edit game settings for this game!",
                    formatted=True,
                )
        else:
            self.__container = LabelComponent(
                f"Settings definition file \"{serial}.settings\" is missing.\n"
                "As a result, we cannot display or edit game settings for this game!",
                formatted=True,
            )

    def __calculate_visible(self) -> None:
        for i, setting in enumerate(self.__settings):
            if isinstance(setting.read_only, ReadOnlyCondition):
                read_only = setting.read_only.evaluate(self.__all_settings)

                self.__labels[i].visible = not read_only
                self.__inputs[i].visible = not read_only
                if read_only and self.__inputs[i].focus:
                    self.__inputs[i].focus = False
                    if i + 1 < len(self.__inputs):
                        self.__inputs[i + 1].focus = True

    def __click_select(self, component: Component, button: str) -> bool:
        if button == Buttons.LEFT:
            if isinstance(component, ClickableSelectInputComponent):
                if not component.visible:
                    return False

            for inp in self.__inputs:
                inp.focus = inp is component
        # Allow this input to continue propagating, so we can focus on and also click
        # the select option dialog.
        return False

    @property
    def dirty(self) -> bool:
        return self.__container.dirty

    def attach(self, scene: "Scene", settings: Dict[str, Any]) -> None:
        self.__container._attach(scene, settings)

    def detach(self) -> None:
        self.__container._detach()

    def render(self, context: RenderContext) -> None:
        self.__container._render(context, context.bounds)

    def tick(self) -> None:
        self.__container.tick()

    def handle_input(self, event: "InputEvent") -> Union[bool, DeferredInput]:
        if isinstance(event, KeyboardInputEvent):
            if event.character == Keys.UP:
                for i, component in enumerate(self.__inputs):
                    if i > 0 and component.focus:
                        # Find previous setting that isn't invisible.
                        for j in range(i - 1, -1, -1):
                            if self.__inputs[j].visible:
                                component.focus = False
                                self.__inputs[j].focus = True
                                return True
                return True
            if event.character == Keys.DOWN:
                for i, component in enumerate(self.__inputs):
                    if i != (len(self.__inputs) - 1) and component.focus:
                        # Find next setting that isn't invisible.
                        for j in range(i + 1, len(self.__inputs)):
                            if self.__inputs[j].visible:
                                component.focus = False
                                self.__inputs[j].focus = True
                                return True
                return True

        inputhandled = self.__container._handle_input(event)

        for i, setting in enumerate(self.__settings):
            for k, v in setting.values.items():
                if v == self.__inputs[i].selected:
                    setting.current = k
                    break
            else:
                raise Exception(f"Logic error! {self.__inputs[i].selected} {setting.values}")
        self.__calculate_visible()

        return inputhandled


class ClickableLabelComponent(LabelComponent):

    callback = None

    def on_click(self: "ClickableLabelComponent", callback: Callable[["ClickableLabelComponent", MouseInputEvent], bool]) -> "ClickableLabelComponent":
        self.callback = callback
        return self

    def handle_input(self, event: "InputEvent") -> Union[bool, DeferredInput]:
        # Overrides handle_input instead of _handle_input because this is
        # meant to be used as either a mixin. This handles input entirely,
        # instead of intercepting it, so thus overriding the public function.
        if isinstance(event, MouseInputEvent):
            if self.callback is not None:
                handled = self.callback(self, event)
                # Fall through to default if the callback didn't handle.
                if bool(handled):
                    return handled
            else:
                # We still handled this regardless of notification
                return True

        return super().handle_input(event)


class EditorScene(Scene):

    def create(self) -> Component:
        return StickyComponent(
            ClickableLabelComponent(
                "<invert> tab - switch settings tab </invert> "
                "<invert> up/down - select setting </invert> "
                "<invert> left/right - change setting </invert> "
                "<invert> q - quit </invert>",
                formatted=True,
            ).on_click(self.__handle_label_click),
            TabComponent(
                [
                    (
                        "&System Settings",
                        SettingsComponent(self.settings["settings"].serial.decode('ascii'), self.settings["settings"].system),
                    ),
                    (
                        "&Game Settings",
                        SettingsComponent(self.settings["settings"].serial.decode('ascii'), self.settings["settings"].game),
                    ),
                ]
            ),
            location=StickyComponent.LOCATION_BOTTOM,
            size=1,
        )

    def save_eeprom(self) -> None:
        # Copy the edited settings over so that the main loop knows we should
        # save them back.
        self.settings['newsettings'] = copy.deepcopy(self.settings['settings'])
        self.main_loop.exit()

    def __handle_label_click(self, component: Component, event: MouseInputEvent) -> bool:
        # This is hacky, we really should define a component and just handle click
        # events without checking position. However, I don't want to work on a
        # list component that sizes each entry to the entry's width/height.
        if event.button == Buttons.LEFT:
            location = component.location
            if location is not None:
                click_x = event.x - location.left
                click_y = event.y - location.top
                if click_y == 0 and click_x >= 85 and click_x <= 94:
                    self.__display_confirm_quit()
                    return True

        return False

    def __display_confirm_quit(self) -> None:
        self.register_component(
            DialogBoxComponent(
                'Write back changes to EEPROM file?',
                [
                    (
                        '&Yes',
                        lambda c, o: self.save_eeprom(),
                    ),
                    (
                        '&No',
                        lambda c, o: self.main_loop.exit(),
                    ),
                    (
                        '&Cancel',
                        lambda c, o: self.unregister_component(c),
                    ),
                ],
            )
        )

    def handle_input(self, event: InputEvent) -> bool:
        if isinstance(event, KeyboardInputEvent):
            if event.character == 'q':
                self.__display_confirm_quit()
                return True

        return False


class SettingsEditor:
    def __init__(self, settings: SettingsWrapper, enable_unicode: bool = True) -> None:
        self.settings = settings
        DragonCursesSettings.enable_unicode = enable_unicode

    def run(self) -> bool:
        # Run the editor, return True if the user requested to save the EEPROM,
        # return False if the user requested to abandon changes.
        os.environ.setdefault('ESCDELAY', '0')

        context = {'settings': copy.deepcopy(self.settings)}
        execute(EditorScene, context)

        # Copy out the values since we want to preserve the input settings by reference.
        if 'newsettings' in context:
            newsettings = context['newsettings']
            for newsetting in newsettings.system.settings:
                for cursetting in self.settings.system.settings:
                    if cursetting.name == newsetting.name:
                        cursetting.current = newsetting.current
                        break
            for newsetting in newsettings.game.settings:
                for cursetting in self.settings.game.settings:
                    if cursetting.name == newsetting.name:
                        cursetting.current = newsetting.current
                        break
            return True
        else:
            return False
