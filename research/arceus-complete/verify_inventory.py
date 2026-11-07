"""Verify archived source hashes and reconstruct standalone runtime patches."""
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def reconstruct(base, patch):
    old = base.splitlines(keepends=True)
    result = []
    cursor = 0
    lines = patch.splitlines(keepends=True)
    index = 0
    while index < len(lines):
        match = re.match(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", lines[index])
        if not match:
            index += 1
            continue
        start = int(match[1]) - (int(match[2] or "1") != 0)
        assert start >= cursor
        result.extend(old[cursor:start])
        cursor = start
        index += 1
        while index < len(lines) and not lines[index].startswith("@@ "):
            line = lines[index]
            if line.startswith((" ", "-")):
                assert cursor < len(old) and old[cursor] == line[1:]
                cursor += 1
            if line.startswith((" ", "+")):
                result.append(line[1:])
            index += 1
    result.extend(old[cursor:])
    return "".join(result).encode()


def main():
    records = json.loads((HERE / "inventory.json").read_text())
    checked = 0
    for record in records:
        if "published" not in record:
            continue
        target = ROOT / record["published"]
        content = target.read_bytes().replace(b"\r\n", b"\n")
        if "base" in record:
            content = reconstruct((ROOT / record["base"]).read_text(encoding="utf-8-sig"), content.decode())
        actual = hashlib.sha256(content).hexdigest()
        assert actual == record["sha256"], f"Source hash differs for {record['source']}"
        checked += 1
    print(f"Verified {checked} source mappings")


if __name__ == "__main__":
    main()
