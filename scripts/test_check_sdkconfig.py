#!/usr/bin/env python3
"""Tests for scripts/check_sdkconfig.py."""

from __future__ import annotations

import io
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import check_sdkconfig


class CompareTest(unittest.TestCase):
    def test_matching_values_pass(self) -> None:
        mismatches, notes = check_sdkconfig.compare(
            'CONFIG_A=y\nCONFIG_B="x"\n', 'CONFIG_A=y\nCONFIG_B="x"\nCONFIG_C=1\n'
        )
        self.assertEqual(mismatches, [])
        self.assertEqual(notes, [])

    def test_stale_value_is_a_mismatch(self) -> None:
        mismatches, _ = check_sdkconfig.compare(
            "CONFIG_LV_USE_CLIB_MALLOC=y\n", "# CONFIG_LV_USE_CLIB_MALLOC is not set\n"
        )
        self.assertEqual(len(mismatches), 1)
        self.assertIn("CONFIG_LV_USE_CLIB_MALLOC", mismatches[0])

    def test_n_equals_not_set(self) -> None:
        mismatches, _ = check_sdkconfig.compare(
            "CONFIG_A=n\n# CONFIG_B is not set\n", "# CONFIG_A is not set\nCONFIG_B=n\n"
        )
        self.assertEqual(mismatches, [])

    def test_comments_are_ignored(self) -> None:
        mismatches, notes = check_sdkconfig.compare("# CONFIG_A=y\n# some text\n", "")
        self.assertEqual(mismatches, [])
        self.assertEqual(notes, [])

    def test_option_missing_from_sdkconfig_is_a_note(self) -> None:
        mismatches, notes = check_sdkconfig.compare("CONFIG_OLD=y\n", "CONFIG_A=y\n")
        self.assertEqual(mismatches, [])
        self.assertEqual(len(notes), 1)


class CheckTest(unittest.TestCase):
    def test_no_sdkconfig_yet_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            defaults = Path(tmp, "sdkconfig.defaults")
            defaults.write_text("CONFIG_A=y\n")
            self.assertTrue(check_sdkconfig.check(defaults, Path(tmp, "sdkconfig")))

    def test_mismatch_fails_and_explains(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            defaults = Path(tmp, "sdkconfig.defaults")
            sdkconfig = Path(tmp, "sdkconfig")
            defaults.write_text("CONFIG_A=y\n")
            sdkconfig.write_text("# CONFIG_A is not set\n")
            err = io.StringIO()
            with redirect_stderr(err), redirect_stdout(io.StringIO()):
                self.assertFalse(check_sdkconfig.check(defaults, sdkconfig))
            self.assertIn("CONFIG_A", err.getvalue())


if __name__ == "__main__":
    unittest.main()
