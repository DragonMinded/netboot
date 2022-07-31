import unittest

# We are importing directly from the implementation because we want to test some
# implementation-specific details here that aren't normally exposed.
from settings.settings import ReadOnlyCondition, Setting, SettingSizeEnum, SettingsSaveException

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
                    values={3, "3"},
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
                    values={4, "4"},
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
                    values={3, "3"},
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
                    values={4, "4"},
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
