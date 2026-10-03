"""Tests use disposable fixtures, never the actual game installation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location(
    "game_ini_fix", Path(__file__).resolve().parents[1] / "apply_inputfix2_game.py")
game_ini_fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(game_ini_fix)


class GameIniTest(unittest.TestCase):
    def test_exact_edit_backup_and_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mai2.ini"
            original = b"\xef\xbb\xbf[AM]\r\nDummyTouchPanel = 1\r\nDummyLED=1\r\n; \xe4\xbf\x9d\xe7\x95\x99\r\n"
            path.write_bytes(original)
            game_ini_fix.apply_ini(path, verify_only=True)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(len(list(Path(directory).iterdir())), 1)
            backup = game_ini_fix.apply_ini(path)
            self.assertEqual(backup.read_bytes(), original)
            self.assertEqual(path.read_bytes(), original.replace(b"Panel = 1", b"Panel = 0"))
            self.assertIsNone(game_ini_fix.apply_ini(path))
            self.assertEqual(len(list(Path(directory).iterdir())), 2)

    def test_ambiguous_or_missing_value_is_untouched(self):
        for content in (b"[AM]\nDummyLED=1\n", b"DummyTouchPanel=2\n",
                        b"DummyTouchPanel=1\nDummyTouchPanel=0\n"):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "mai2.ini"
                path.write_bytes(content)
                with self.assertRaises(ValueError):
                    game_ini_fix.apply_ini(path)
                self.assertEqual(path.read_bytes(), content)
                self.assertEqual(len(list(Path(directory).iterdir())), 1)


if __name__ == "__main__":
    unittest.main()
