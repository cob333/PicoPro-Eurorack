#!/usr/bin/env python3
# Copyright 2026 Wenhao Yang
#
# Author: Wenhao Yang
# Contributor: Wenhao Yang
#
# Generate an Arduino-Pico linker script whose FLASH origin starts at an app slot.

import argparse
from pathlib import Path


def int_arg(value: str) -> int:
    lowered = value.strip().lower()
    if lowered.endswith("k"):
        return int(lowered[:-1], 0) * 1024
    if lowered.endswith("m"):
        return int(lowered[:-1], 0) * 1024 * 1024
    return int(lowered, 0)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a slot-addressed Arduino-Pico memmap_default.ld"
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--flash-origin", required=True, type=int_arg)
    parser.add_argument("--flash-length", required=True, type=int_arg)
    parser.add_argument("--eeprom-start", required=True, type=int_arg)
    parser.add_argument("--fs-start", required=True, type=int_arg)
    parser.add_argument("--fs-end", required=True, type=int_arg)
    parser.add_argument("--ram-length", required=True, type=int_arg)
    parser.add_argument("--psram-length", required=True, type=int_arg)
    args = parser.parse_args()

    text = args.input.read_text()
    text = text.replace(
        "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = __FLASH_LENGTH__",
        f"FLASH(rx) : ORIGIN = 0x{args.flash_origin:08x}, LENGTH = __FLASH_LENGTH__",
    )
    replacements = {
        "__FLASH_LENGTH__": str(args.flash_length),
        "__EEPROM_START__": str(args.eeprom_start),
        "__FS_START__": str(args.fs_start),
        "__FS_END__": str(args.fs_end),
        "__RAM_LENGTH__": str(args.ram_length),
        "__PSRAM_LENGTH__": str(args.psram_length),
    }
    for src, dst in replacements.items():
        text = text.replace(src, dst)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)


if __name__ == "__main__":
    main()
