import unittest

# We are importing directly from the implementation because we want to test some
# implementation-specific details here that aren't normally exposed.
from settings.settings import ReadOnlyCondition, DefaultCondition, DefaultConditionGroup, Setting, Settings, SettingSizeEnum, SettingsSaveException


class TestReadOnlyCondition(unittest.TestCase):
    def test_evaluate_normal(self) -> None:
        roc = ReadOnlyCondition(
            filename="foo.settings",
            setting="self",
            name="other",
            values=[3, 5, 7],
            negate=False
        )

        # Verify that if the other setting is set to one of the values in the
        # above list, the evaluation returns false (not read-only).
        self.assertFalse(
            roc.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={3: "3"},
                    current=3,
                )
            ])
        )

        # Verify that if the other setting is set to a different value than one
        # that we're listing, the evaluation returns true (read-only).
        self.assertTrue(
            roc.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={4: "4"},
                    current=4,
                )
            ])
        )

    def test_evaluate_negate(self) -> None:
        roc = ReadOnlyCondition(
            filename="foo.settings",
            setting="self",
            name="other",
            values=[3, 5, 7],
            negate=True
        )

        # Verify that if the other setting is set to one of the values in the
        # above list, the evaluation returns true (not read-only, negated).
        self.assertTrue(
            roc.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={3: "3"},
                    current=3,
                )
            ])
        )

        # Verify that if the other setting is set to a different value than one
        # that we're listing, the evaluation returns false (read-only, negated).
        self.assertFalse(
            roc.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={4: "4"},
                    current=4,
                )
            ])
        )

    def test_evaluate_not_found(self) -> None:
        roc = ReadOnlyCondition(
            filename="foo.settings",
            setting="self",
            name="other",
            values=[3, 5, 7],
            negate=True
        )

        # Give a list of settings that doesn't include "other", which is an error since the setting
        # group should include the setting that the read only condition is pointing at during evaluation.
        with self.assertRaises(SettingsSaveException) as exc:
            roc.evaluate([
                Setting(
                    name="bla",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                )
            ])

        # Verify that the thrown exception gives out the filename and expected setting name.
        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "The setting \"self\" depends on the value for \"other\" but that setting does not seem to exist! Perhaps you misspelled \"other\"?")


class TestDefaultConditionGroup(unittest.TestCase):
    def test_evaluate_normal(self) -> None:
        dcg = DefaultConditionGroup(
            filename="foo.settings",
            setting="self",
            conditions=[
                # Set this setting to "10" when the other setting is "1".
                DefaultCondition(
                    name="other",
                    values=[1],
                    negate=False,
                    default=10,
                ),
                # Set this setting to "20" when the other setting is "2".
                DefaultCondition(
                    name="other",
                    values=[2],
                    negate=False,
                    default=20,
                ),
            ]
        )

        # Verify that we calculate the right setting when the other setting value is presented.
        self.assertEqual(
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off"},
                    current=1,
                )
            ]),
            10
        )
        self.assertEqual(
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off"},
                    current=2,
                )
            ]),
            20
        )

    def test_evaluate_negate(self) -> None:
        dcg = DefaultConditionGroup(
            filename="foo.settings",
            setting="self",
            conditions=[
                # Set this setting to "10" when the other setting is "1".
                DefaultCondition(
                    name="other",
                    values=[1],
                    negate=False,
                    default=10,
                ),
                # Set this setting to "20" when the other setting is "2".
                DefaultCondition(
                    name="other",
                    values=[2],
                    negate=False,
                    default=20,
                ),
                # Set this setting to "30" when the other setting is not "1" or "2".
                DefaultCondition(
                    name="other",
                    values=[1, 2],
                    negate=True,
                    default=30,
                ),
            ]
        )

        # Verify that we calculate the right setting when we negate the other setting value
        # (basically "default is x when y is not z" instead of "default is x when y is z".
        self.assertEqual(
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off", 3: "Disabled"},
                    current=1,
                )
            ]),
            10
        )
        self.assertEqual(
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off", 3: "Disabled"},
                    current=2,
                )
            ]),
            20
        )
        self.assertEqual(
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off", 3: "Disabled"},
                    current=3,
                )
            ]),
            30
        )

    def test_evaluate_not_found(self) -> None:
        dcg = DefaultConditionGroup(
            filename="foo.settings",
            setting="self",
            conditions=[
                # Set this setting to "10" when the other setting is "1".
                DefaultCondition(
                    name="other",
                    values=[1],
                    negate=False,
                    default=10,
                ),
                # Set this setting to "20" when the other setting is "2".
                DefaultCondition(
                    name="other",
                    values=[2],
                    negate=False,
                    default=20,
                ),
            ]
        )

        # Verify that the thrown exception gives out the filename and expected setting name.
        with self.assertRaises(SettingsSaveException) as exc:
            dcg.evaluate([
                Setting(
                    name="other",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off", 3: "Default"},
                    current=3,
                )
            ])

        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "The default for setting \"self\" could not be determined! Perhaps you misspelled one of \"other\", or you forgot a value?")

        with self.assertRaises(SettingsSaveException) as exc:
            dcg.evaluate([
                Setting(
                    name="different",
                    order=0,
                    size=SettingSizeEnum.BYTE,
                    length=1,
                    read_only=False,
                    values={1: "On", 2: "Off"},
                    current=1,
                )
            ])

        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "The default for setting \"self\" could not be determined! Perhaps you misspelled one of \"other\", or you forgot a value?")


class TestSetting(unittest.TestCase):
    def test_roundtrip(self) -> None:
        before = Setting(
            name="foo",
            order=5,
            size=SettingSizeEnum.BYTE,
            length=2,
            read_only=False,
            values={1: "1", 2: "2"},
            current=1,
            default=2,
        )
        after = Setting.from_json("file.settings", before.to_json(), [])

        self.assertEqual(before.name, after.name)
        self.assertEqual(before.order, after.order)
        self.assertEqual(before.size, after.size)
        self.assertEqual(before.length, after.length)
        self.assertEqual(before.read_only, after.read_only)
        self.assertEqual(before.values, after.values)
        self.assertEqual(before.current, after.current)
        self.assertEqual(before.default, after.default)

        before = Setting(
            name="foo",
            order=5,
            size=SettingSizeEnum.BYTE,
            length=2,
            read_only=ReadOnlyCondition(filename="file.settings", setting="foo", name="bar", values=[1], negate=False),
            values={1: "1", 2: "2"},
            current=1,
            default=2,
        )
        after = Setting.from_json("file.settings", before.to_json(), [])

        self.assertEqual(before.name, after.name)
        self.assertEqual(before.order, after.order)
        self.assertEqual(before.size, after.size)
        self.assertEqual(before.length, after.length)
        self.assertEqual(before.read_only, after.read_only)
        self.assertEqual(before.values, after.values)
        self.assertEqual(before.current, after.current)
        self.assertEqual(before.default, after.default)

        before = Setting(
            name="foo",
            order=5,
            size=SettingSizeEnum.BYTE,
            length=2,
            read_only=True,
            values={1: "1", 2: "2"},
            current=1,
            default=DefaultConditionGroup(
                filename="file.settings",
                setting="foo",
                conditions=[
                    DefaultCondition(
                        name="bar",
                        values=[1],
                        negate=False,
                        default=10,
                    ),
                    DefaultCondition(
                        name="bar",
                        values=[2],
                        negate=False,
                        default=20,
                    ),
                    DefaultCondition(
                        name="bar",
                        values=[1, 2],
                        negate=True,
                        default=30,
                    )
                ],
            ),
        )
        after = Setting.from_json("file.settings", before.to_json(), [])

        self.assertEqual(before.name, after.name)
        self.assertEqual(before.order, after.order)
        self.assertEqual(before.size, after.size)
        self.assertEqual(before.length, after.length)
        self.assertEqual(before.read_only, after.read_only)
        self.assertEqual(before.values, after.values)
        self.assertEqual(before.current, after.current)
        self.assertEqual(before.default, after.default)


class TestSettings(unittest.TestCase):
    def test_length(self) -> None:
        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                ],
            ).length,
            0
        )

        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=1,
                        read_only=False,
                    ),
                ],
            ).length,
            1
        )

        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=2,
                        read_only=False,
                    ),
                ],
            ).length,
            2
        )

        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=4,
                        read_only=False,
                    ),
                ],
            ).length,
            4
        )

        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                    Setting(
                        name="bar",
                        order=1,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                ],
            ).length,
            1
        )

        self.assertEqual(
            Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=2,
                        read_only=False,
                    ),
                    Setting(
                        name="foo",
                        order=1,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                    Setting(
                        name="bar",
                        order=2,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=1,
                        read_only=False,
                    ),
                ],
            ).length,
            4
        )

        with self.assertRaises(SettingsSaveException) as exc:
            _ = Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                ],
            ).length,

        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "The setting \"foo\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!")

        with self.assertRaises(SettingsSaveException) as exc:
            _ = Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="foo",
                        order=0,
                        size=SettingSizeEnum.NIBBLE,
                        length=1,
                        read_only=False,
                    ),
                    Setting(
                        name="bar",
                        order=1,
                        size=SettingSizeEnum.BYTE,
                        length=1,
                        read_only=False,
                    ),
                ],
            ).length,

        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "The setting \"bar\" follows a lonesome half-byte setting \"foo\". Half-byte settings must always be in pairs!")

        with self.assertRaises(SettingsSaveException) as exc:
            _ = Settings(
                filename="foo.settings",
                settings=[
                    Setting(
                        name="bar",
                        order=0,
                        size=SettingSizeEnum.BYTE,
                        length=3,
                        read_only=False,
                    ),
                ],
            ).length,

        self.assertEqual(exc.exception.filename, "foo.settings")
        self.assertEqual(str(exc.exception), "Cannot save setting \"bar\" with unrecognized size 3!")