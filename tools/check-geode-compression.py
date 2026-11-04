"""Fail if a .geode uses compression unsupported by the Geode iOS loader.

Only stored (0) and deflate (8) entries are supported by the iOS loader.
This checks the ZIP central directory and does not need a decompressor.
"""

import sys
import zipfile
from pathlib import Path

SAFE = {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}
NAMES = {
    zipfile.ZIP_STORED: "store",
    zipfile.ZIP_DEFLATED: "deflate",
    12: "bzip2",
    14: "lzma",
    93: "zstd",
    95: "xz",
}


def check(path: Path) -> bool:
    try:
        with zipfile.ZipFile(path) as archive:
            methods = {entry.compress_type for entry in archive.infolist()}
    except (OSError, zipfile.BadZipFile) as error:
        print(f"{path}: BAD - cannot read archive: {error}")
        return False

    unsafe = sorted(method for method in methods if method not in SAFE)
    readable = ", ".join(NAMES.get(method, str(method)) for method in sorted(methods))
    if unsafe:
        print(f"{path}: BAD - {readable}")
        return False

    print(f"{path}: ok - {readable}")
    return True


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: check-geode-compression.py <file.geode|directory> [...]")
        return 2

    targets: list[Path] = []
    for argument in argv[1:]:
        path = Path(argument)
        if path.is_dir():
            targets.extend(sorted(path.rglob("*.geode")))
        elif path.is_file():
            targets.append(path)

    if not targets:
        print("no .geode files found")
        return 1

    results = [check(target) for target in targets]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
