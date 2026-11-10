#!/usr/bin/env python3
"""
Convert repository source files to their required UTF-8 formats.

.h and .cpp files are converted to UTF-8 with BOM.
.hlsl and .hlsli files are converted to UTF-8 without BOM.

The default mode is a dry run. Use --write to apply the conversion.
All files are decoded before any file is written, so a decoding error
cannot leave the repository partially converted.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


SOURCE_FORMATS = {
    ".h": "utf-8-sig",
    ".cpp": "utf-8-sig",
    ".hlsl": "utf-8",
    ".hlsli": "utf-8",
}
SKIP_DIRECTORIES = {".git"}


def decode_source(data: bytes) -> tuple[str, str]:
    """Decode source bytes and return (text, detected encoding)."""
    if data.startswith(b"\xef\xbb\xbf"):
        return data[3:].decode("utf-8", errors="strict"), "UTF-8 with BOM"

    if data.startswith(b"\xff\xfe\x00\x00"):
        return data[4:].decode("utf-32-le", errors="strict"), "UTF-32 LE"
    if data.startswith(b"\x00\x00\xfe\xff"):
        return data[4:].decode("utf-32-be", errors="strict"), "UTF-32 BE"
    if data.startswith(b"\xff\xfe"):
        return data[2:].decode("utf-16-le", errors="strict"), "UTF-16 LE"
    if data.startswith(b"\xfe\xff"):
        return data[2:].decode("utf-16-be", errors="strict"), "UTF-16 BE"

    try:
        return data.decode("utf-8", errors="strict"), "UTF-8"
    except UnicodeDecodeError:
        # CP932 is used by Japanese Windows/Visual Studio projects.
        return data.decode("cp932", errors="strict"), "CP932"


def collect_source_files(root: Path) -> list[Path]:
    """Collect source files while pruning excluded directories."""
    files: list[Path] = []

    for current, directories, filenames in os.walk(root):
        directories[:] = sorted(
            name for name in directories if name not in SKIP_DIRECTORIES
        )
        current_path = Path(current)
        for filename in filenames:
            path = current_path / filename
            if path.suffix.lower() in SOURCE_FORMATS:
                files.append(path)

    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Convert .h/.cpp files to UTF-8 with BOM and "
            ".hlsl/.hlsli files to UTF-8 without BOM."
        )
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root (defaults to the parent directory of this script's folder).",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Write converted files. Without this option, perform a dry run.",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"Error: directory does not exist: {root}", file=sys.stderr)
        return 2

    files = collect_source_files(root)
    converted_files: list[tuple[Path, bytes, str]] = []
    already_utf8 = 0
    errors: list[tuple[Path, Exception]] = []

    # Preflight every file before writing anything.
    for path in files:
        try:
            original = path.read_bytes()
            decoded, source_encoding = decode_source(original)
            target_encoding = SOURCE_FORMATS[path.suffix.lower()]
            utf8_data = decoded.encode(target_encoding)

            if original == utf8_data:
                already_utf8 += 1
            else:
                converted_files.append((path, utf8_data, source_encoding))
        except (OSError, UnicodeError) as error:
            errors.append((path, error))

    for path, error in errors:
        print(f"[ERROR] {path.relative_to(root)}: {error}", file=sys.stderr)

    if errors:
        print()
        print(f"Files scanned: {len(files)}")
        print(f"Already in target format: {already_utf8}")
        print(f"Would convert: {len(converted_files)}")
        print(f"Errors: {len(errors)}")
        print("Aborted: no files were modified.")
        return 1

    for path, utf8_data, source_encoding in converted_files:
        relative_path = path.relative_to(root)
        print(
            f"{'[WRITE]' if args.write else '[DRY-RUN]'} "
            f"{relative_path} ({source_encoding} -> UTF-8)"
        )

    if args.write:
        for path, utf8_data, _ in converted_files:
            path.write_bytes(utf8_data)

        # Verify every source file after writing.
        verification_errors: list[Path] = []
        for path in files:
            try:
                data = path.read_bytes()
                expects_bom = SOURCE_FORMATS[path.suffix.lower()] == "utf-8-sig"
                has_bom = data.startswith(b"\xef\xbb\xbf")
                if expects_bom != has_bom:
                    expected = "present" if expects_bom else "absent"
                    raise UnicodeError(f"UTF-8 BOM must be {expected}")
                if has_bom:
                    data = data[3:]
                data.decode("utf-8", errors="strict")
            except (OSError, UnicodeError):
                verification_errors.append(path)

        if verification_errors:
            for path in verification_errors:
                print(
                    f"[VERIFY-ERROR] {path.relative_to(root)}",
                    file=sys.stderr,
                )
            print("UTF-8 verification failed.", file=sys.stderr)
            return 1

    print()
    print(f"Files scanned: {len(files)}")
    print(f"Already in target format: {already_utf8}")
    print(f"{'Converted' if args.write else 'Would convert'}: {len(converted_files)}")
    print("Errors: 0")

    if not args.write and converted_files:
        print("Run again with --write to apply the conversion.")
    elif args.write:
        print("Per-extension UTF-8 format verification: passed")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
