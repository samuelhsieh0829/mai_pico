"""Back up mai2.ini and change only DummyTouchPanel=1 to 0; never stop the game."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import re
import uuid


def apply_ini(path: Path, verify_only: bool = False) -> Path | None:
    original = path.read_bytes()
    pattern = rb"(?m)^(DummyTouchPanel[ \t]*=[ \t]*)([01])(?=[ \t]*\r?$)"
    matches = list(re.finditer(pattern, original))
    if len(matches) != 1:
        raise ValueError("Expected exactly one DummyTouchPanel=0 or 1; no files changed")
    if matches[0][2] == b"0":
        print(f"Already configured: DummyTouchPanel=0 in {path}")
        return None
    # Edit one ASCII byte; retain encoding, BOM, comments, and line endings.
    position = matches[0].start(2)
    updated = original[:position] + b"0" + original[position + 1:]
    if verify_only:
        print(f"Verified change: DummyTouchPanel=1 -> 0 in {path}; no files written")
        return None
    suffix = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    backup = path.with_name(path.name + ".before-inputfix2-" + suffix + ".bak")
    with backup.open("xb") as stream:
        stream.write(original)
    if backup.read_bytes() != original:
        raise OSError(f"Backup verification failed; original unchanged. Backup: {backup}")
    if path.read_bytes() != original:
        raise OSError(f"INI changed concurrently; original left alone. Backup: {backup}")
    try:
        path.write_bytes(updated)
        if path.read_bytes() != updated:
            raise OSError("INI readback mismatch")
    except OSError:
        path.write_bytes(original)
        raise
    print(f"Applied and verified: DummyTouchPanel=0 in {path}")
    print(f"Backup: {backup}")
    print("Exit the game normally and restart before testing. No process was stopped.")
    return backup


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ini", type=Path, default=Path("D:/SDGA_1.65-A/Package/mai2.ini"))
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    apply_ini(args.ini, args.verify_only)
