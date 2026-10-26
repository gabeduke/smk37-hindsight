# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Tests for tools/flash-diff.py, on synthetic 1 MiB images. Run by tests/run.sh."""
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "flash-diff.py")
SIZE = 0x100000


class FlashDiff(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.before = bytearray(b"\xff" * SIZE)
        self.before[0xC1224:0xC122C] = b"trimdat\0"

    def tearDown(self):
        self.dir.cleanup()

    def run_tool(self, after, *args, before=None):
        paths = []
        for name, data in (("before.bin", before or self.before), ("after.bin", after)):
            path = os.path.join(self.dir.name, name)
            with open(path, "wb") as f:
                f.write(data)
            paths.append(path)
        done = subprocess.run([sys.executable, "-I", TOOL, *paths, *args], capture_output=True, text=True)
        return done.returncode, done.stdout

    def changed(self, *spans):
        after = bytearray(self.before)
        for at, value in spans:
            after[at] = value
        return after

    def test_identical(self):
        self.assertEqual(self.run_tool(bytes(self.before))[0], 0)

    def test_firmware_change_inside_expected(self):
        code, out = self.run_tool(self.changed((0x4000, 0), (0x7AFFF, 1)), "--expect", "0x4000-0x7B000")
        self.assertEqual(code, 0, out)

    def test_change_outside_expected(self):
        code, out = self.run_tool(self.changed((0x4000, 0), (0x7B000, 1)), "--expect", "0x4000-0x7B000")
        self.assertEqual(code, 1)
        self.assertIn("outside the expected ranges from 0x7B000", out)

    def test_calibration_change_always_fails(self):
        code, out = self.run_tool(self.changed((0xC1224, 0)), "--expect", "0x0-0x100000")
        self.assertEqual(code, 1)
        self.assertIn("UNIT DATA CHANGED", out)

    def test_bluetooth_change_fails_without_expect(self):
        self.assertEqual(self.run_tool(self.changed((0xC0000, 0)))[0], 1)

    def test_accepted_guard_copy(self):
        after = self.changed((0xFF000, ord("T")), (0xFF23B, 0))
        code, out = self.run_tool(after, "--accept-unit", "0xFF000-0xFF23C")
        self.assertEqual(code, 0, out)

    def test_accepted_range_is_exact(self):
        after = self.changed((0xFF010, 0), (0xFFF40, 0))
        code, out = self.run_tool(after, "--accept-unit", "0xFF000-0xFF23C")
        self.assertEqual(code, 1)
        self.assertIn("0xFFF40", out)

    def test_size_mismatch(self):
        self.assertEqual(self.run_tool(bytes(SIZE - 1))[0], 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
