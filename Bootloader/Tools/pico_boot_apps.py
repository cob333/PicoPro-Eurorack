#!/usr/bin/env python3
import argparse
import json
import os
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
XIP_BASE = 0x10000000

PICO_SELECTOR_MAGIC = 0x50494253
PICO_SELECTOR_VERSION = 5
PICO_SELECTOR_MAX_APPS = 20
PICO_SELECTOR_BOOT_BYTES = 256 * 1024
PICO_SELECTOR_CONFIG_A_OFFSET = PICO_SELECTOR_BOOT_BYTES - (2 * 4096)
PICO_SELECTOR_CONFIG_B_OFFSET = PICO_SELECTOR_BOOT_BYTES - 4096
PICO_SELECTOR_FIRST_APP_OFFSET = PICO_SELECTOR_BOOT_BYTES
PICO_SELECTOR_DEFAULT_SLOT_BYTES = 512 * 1024
PICO_SELECTOR_DEFAULT_APP_REGION_BYTES = 3584 * 1024
PICO_SELECTOR_ARDUINO_VECTOR_OFFSET = 0x3000
PICO_SELECTOR_FLAG_VALID = 0x01
PICO_SELECTOR_FLAG_RP2040 = 0x02
PICO_SELECTOR_FLAG_RP2350 = 0x04
PICO_SELECTOR_FLAG_PICOFX = 0x08
FLASH_SECTOR_SIZE = 4096
UF2_PAYLOAD_SIZE = 256
CONFIG_RECORD_SIZE = 52 + (PICO_SELECTOR_MAX_APPS * 20) + 4
DEFAULT_CPU_HZ = 250_000_000
DEFAULT_FQBN = "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=250"
UF2_TRAILING_MARKER_MAX_BYTES = UF2_PAYLOAD_SIZE
DEFAULT_DYNAMIC_SLOT_PADDING = 0


@dataclass
class Uf2Block:
    flags: int
    target: int
    payload: bytes
    family: int


@dataclass(frozen=True)
class SlotApp:
    slot: int
    path: Path


@dataclass
class SlotLayout:
    slot: int
    path: Path
    blocks: list[Uf2Block]
    flash_offset: int
    max_size: int
    used_size: int


def int_arg(value: str) -> int:
    return int(value, 0)


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        raise ValueError("alignment must be greater than zero")
    return ((value + alignment - 1) // alignment) * alignment


def slot_app_arg(value: str) -> SlotApp:
    if "=" not in value:
        raise argparse.ArgumentTypeError("slot app must use SLOT=PATH")
    slot_text, path_text = value.split("=", 1)
    try:
        slot = int(slot_text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid slot index: {slot_text}") from exc
    return SlotApp(slot=slot, path=Path(path_text))


def fnv1a32(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def read_uf2(path: Path) -> list[Uf2Block]:
    raw = path.read_bytes()
    if len(raw) % 512:
        raise ValueError(f"{path} is not a whole-block UF2 file")

    blocks: list[Uf2Block] = []
    for offset in range(0, len(raw), 512):
        block = raw[offset : offset + 512]
        start0, start1, flags, target, size, _block_no, _num_blocks, family = struct.unpack_from(
            "<IIIIIIII", block, 0
        )
        end_magic = struct.unpack_from("<I", block, 508)[0]
        if start0 != UF2_MAGIC_START0 or start1 != UF2_MAGIC_START1 or end_magic != UF2_MAGIC_END:
            raise ValueError(f"{path} has an invalid UF2 block at {offset}")
        if size > 476:
            raise ValueError(f"{path} has an invalid UF2 payload size {size}")
        blocks.append(Uf2Block(flags, target, block[32 : 32 + size], family))
    return blocks


def write_uf2(path: Path, blocks: Iterable[Uf2Block]) -> None:
    materialized = list(blocks)
    out = bytearray()
    total = len(materialized)
    for index, block in enumerate(materialized):
        payload = block.payload
        if len(payload) > 476:
            raise ValueError("UF2 payload is too large")
        record = bytearray(512)
        struct.pack_into(
            "<IIIIIIII",
            record,
            0,
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            block.flags,
            block.target,
            len(payload),
            index,
            total,
            block.family,
        )
        record[32 : 32 + len(payload)] = payload
        struct.pack_into("<I", record, 508, UF2_MAGIC_END)
        out.extend(record)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)


def family_from(blocks: list[Uf2Block]) -> tuple[int, int]:
    for block in blocks:
        if block.flags & UF2_FLAG_FAMILY_ID:
            return block.flags, block.family
    return 0, 0


def flash_bytes(blocks: list[Uf2Block]) -> dict[int, int]:
    data: dict[int, int] = {}
    for block in blocks:
        if block.target < XIP_BASE:
            continue
        for index, byte in enumerate(block.payload):
            data[block.target + index] = byte
    return data


def read_u32(memory: dict[int, int], addr: int) -> int | None:
    try:
        return (
            memory[addr]
            | (memory[addr + 1] << 8)
            | (memory[addr + 2] << 16)
            | (memory[addr + 3] << 24)
        )
    except KeyError:
        return None


def app_flash_ranges(blocks: list[Uf2Block]) -> list[tuple[int, int]]:
    ranges: list[list[int]] = []
    for block in sorted((block for block in blocks if block.target >= XIP_BASE), key=lambda item: item.target):
        start = block.target
        end = block.target + len(block.payload)
        if not ranges or start > ranges[-1][1]:
            ranges.append([start, end])
        else:
            ranges[-1][1] = max(ranges[-1][1], end)

    if not ranges:
        return []

    if len(ranges) > 1 and ranges[-1][1] - ranges[-1][0] <= UF2_TRAILING_MARKER_MAX_BYTES:
        ranges.pop()

    return [(start, end) for start, end in ranges]


def app_flash_span(blocks: list[Uf2Block]) -> tuple[int, int]:
    ranges = app_flash_ranges(blocks)
    if not ranges:
        return 0, 0
    return min(start for start, _end in ranges), max(end for _start, end in ranges)


def app_usage_bytes(blocks: list[Uf2Block]) -> int:
    start, end = app_flash_span(blocks)
    return end - start


def validate_slot_app(
    path: Path, blocks: list[Uf2Block], slot_offset: int, slot_size: int, vector_offset: int
) -> None:
    memory = flash_bytes(blocks)
    slot_base = XIP_BASE + slot_offset
    vector_addr = slot_base + vector_offset
    stack = read_u32(memory, vector_addr)
    reset = read_u32(memory, vector_addr + 4)
    if stack is None or reset is None:
        raise ValueError(f"{path} has no vector words at 0x{vector_addr:08x}")
    if not (0x20000000 <= stack <= 0x20082000):
        raise ValueError(f"{path} has an invalid stack pointer 0x{stack:08x}")
    if not (slot_base <= reset < slot_base + slot_size) or not (reset & 1):
        raise ValueError(
            f"{path} is not linked for slot 0x{slot_offset:x}: reset vector is 0x{reset:08x}"
        )


def config_record(
    app_count: int,
    active: int,
    layouts: dict[int, SlotLayout],
    target_flag: int,
    sequence: int,
    cpu_hz: int,
) -> bytes:
    if not 0 <= active < app_count:
        raise ValueError("active app index is out of range")
    if app_count > PICO_SELECTOR_MAX_APPS:
        raise ValueError(f"only {PICO_SELECTOR_MAX_APPS} app slots are supported")
    if active not in layouts:
        raise ValueError("active app slot is empty")

    record = bytearray(CONFIG_RECORD_SIZE)
    struct.pack_into(
        "<IHHIBBHI",
        record,
        0,
        PICO_SELECTOR_MAGIC,
        PICO_SELECTOR_VERSION,
        CONFIG_RECORD_SIZE,
        sequence,
        active,
        app_count,
        0,
        cpu_hz,
    )
    struct.pack_into(
        "<8f",
        record,
        20,
        582.52,
        582.52,
        5456.0,
        4095.0,
        4095.0,
        0.0,
        0.0,
        0.0,
    )
    for index in range(PICO_SELECTOR_MAX_APPS):
        flags = 0
        app_id = 0
        flash_offset = 0
        max_size = 0
        vector_offset = PICO_SELECTOR_ARDUINO_VECTOR_OFFSET
        if index < app_count and index in layouts:
            layout = layouts[index]
            flags = PICO_SELECTOR_FLAG_VALID | target_flag
            app_id = index + 1
            flash_offset = layout.flash_offset
            max_size = layout.max_size
        struct.pack_into(
            "<IIIII",
            record,
            52 + (index * 20),
            flash_offset,
            max_size,
            flags,
            app_id,
            vector_offset,
        )
    crc = fnv1a32(record[:-4])
    struct.pack_into("<I", record, CONFIG_RECORD_SIZE - 4, crc)
    return bytes(record)


def sector_blocks(flash_offset: int, record: bytes, flags: int, family: int) -> list[Uf2Block]:
    sector = bytearray([0xFF] * FLASH_SECTOR_SIZE)
    sector[: len(record)] = record
    blocks = []
    for pos in range(0, len(sector), UF2_PAYLOAD_SIZE):
        blocks.append(
            Uf2Block(
                flags=flags,
                target=XIP_BASE + flash_offset + pos,
                payload=bytes(sector[pos : pos + UF2_PAYLOAD_SIZE]),
                family=family,
            )
        )
    return blocks


def build_slot(args: argparse.Namespace) -> None:
    script = Path(__file__).with_name("make_slot_memmap.py").resolve()
    slot_offset = (
        args.slot_offset
        if args.slot_offset is not None
        else args.first_app_offset + (args.slot * args.slot_size)
    )
    flash_origin = XIP_BASE + slot_offset
    recipe = (
        f'"{sys.executable}" -I "{script}" '
        f'--input "{{runtime.platform.path}}/lib/{{build.chip}}/memmap_default.ld" '
        f'--out "{{build.path}}/memmap_default.ld" '
        f"--flash-origin 0x{flash_origin:08x} "
        f"--flash-length {args.slot_size} "
        f'--eeprom-start "{{build.eeprom_start}}" '
        f'--fs-start "{{build.fs_start}}" '
        f'--fs-end "{{build.fs_end}}" '
        f'--ram-length "{{build.ram_length}}" '
        f'--psram-length "{{build.psram_length}}"'
    )
    cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        args.fqbn,
        "--build-path",
        str(args.build_path),
        "--build-property",
        f"recipe.hooks.linking.prelink.1.pattern={recipe}",
    ]
    for library in args.library:
        cmd.extend(["--libraries", str(library)])
    cmd.append(str(args.sketch))
    env = os.environ.copy()
    toolchain = Path.home() / "Library/Arduino15/packages/rp2040/tools/pqt-gcc/4.1.0-1aec55e/bin"
    env["PATH"] = f"{toolchain}{os.pathsep}{env.get('PATH', '')}"
    subprocess.run(cmd, check=True, env=env)


def package(args: argparse.Namespace) -> None:
    selector_blocks = read_uf2(args.selector)
    flags, family = family_from(selector_blocks)
    if not flags:
        raise ValueError(f"{args.selector} does not contain a UF2 family id")

    if args.app and args.slot_app:
        raise ValueError("use either --app or --slot-app, not both")
    if args.slot_app:
        slot_apps = sorted(args.slot_app, key=lambda item: item.slot)
    elif args.app:
        slot_apps = [SlotApp(slot=index, path=path) for index, path in enumerate(args.app)]
    else:
        raise ValueError("at least one --app or --slot-app is required")

    seen_slots: set[int] = set()
    for item in slot_apps:
        if not 0 <= item.slot < PICO_SELECTOR_MAX_APPS:
            raise ValueError(f"slot {item.slot} is out of range")
        if item.slot in seen_slots:
            raise ValueError(f"slot {item.slot} is specified more than once")
        seen_slots.add(item.slot)

    app_blocks: list[Uf2Block] = []
    layouts: dict[int, SlotLayout] = {}
    target_flag = PICO_SELECTOR_FLAG_RP2350 if args.target == "rp2350" else PICO_SELECTOR_FLAG_RP2040
    region_end = args.first_app_offset + args.app_region_size
    for item in slot_apps:
        path = item.path
        blocks = read_uf2(path)
        if args.fixed_slots:
            slot_offset = args.first_app_offset + (item.slot * args.slot_size)
            slot_size = args.slot_size
        else:
            span_start, span_end = app_flash_span(blocks)
            if span_start < XIP_BASE:
                raise ValueError(f"{path} does not contain flash payload blocks")
            slot_offset = span_start - XIP_BASE
            slot_size = align_up((span_end - span_start) + args.slot_padding, args.slot_align)
        validate_slot_app(path, blocks, slot_offset, slot_size, args.vector_offset)
        used = app_usage_bytes(blocks)
        if used > slot_size:
            raise ValueError(
                f"{path} uses {used} bytes and does not fit in a {slot_size} byte slot"
            )
        if slot_offset < args.first_app_offset:
            raise ValueError(f"{path} starts before the app flash region")
        if slot_offset + slot_size > region_end:
            raise ValueError(
                f"{path} ends at 0x{slot_offset + slot_size:x}, beyond app region end 0x{region_end:x}"
            )
        slot_base = XIP_BASE + slot_offset
        slot_end = slot_base + slot_size
        for layout in layouts.values():
            other_start = XIP_BASE + layout.flash_offset
            other_end = other_start + layout.max_size
            if slot_base < other_end and other_start < slot_end:
                raise ValueError(f"{path} overlaps slot {layout.slot}")
        layouts[item.slot] = SlotLayout(
            slot=item.slot,
            path=path,
            blocks=blocks,
            flash_offset=slot_offset,
            max_size=slot_size,
            used_size=used,
        )
        app_blocks.extend(block for block in blocks if slot_base <= block.target < slot_end)

    app_count = max(seen_slots) + 1
    record_a = config_record(
        app_count,
        args.active,
        layouts,
        target_flag,
        1,
        args.cpu_hz,
    )
    record_b = config_record(
        app_count,
        args.active,
        layouts,
        target_flag,
        2,
        args.cpu_hz,
    )
    config_blocks = sector_blocks(PICO_SELECTOR_CONFIG_A_OFFSET, record_a, flags, family)
    config_blocks.extend(sector_blocks(PICO_SELECTOR_CONFIG_B_OFFSET, record_b, flags, family))
    write_uf2(args.output, [*selector_blocks, *app_blocks, *config_blocks])
    if args.layout_json:
        args.layout_json.parent.mkdir(parents=True, exist_ok=True)
        args.layout_json.write_text(
            json.dumps(
                [
                    {
                        "slot": layout.slot,
                        "path": str(layout.path),
                        "flashOffset": layout.flash_offset,
                        "flashOffsetHex": f"0x{layout.flash_offset:x}",
                        "maxSize": layout.max_size,
                        "maxSizeHex": f"0x{layout.max_size:x}",
                        "usedSize": layout.used_size,
                    }
                    for layout in sorted(layouts.values(), key=lambda item: item.slot)
                ],
                indent=2,
            )
            + "\n"
        )


def measure(args: argparse.Namespace) -> None:
    rows = []
    next_offset = args.first_app_offset
    region_end = args.first_app_offset + args.app_region_size
    for path in args.uf2:
        blocks = read_uf2(path)
        used = app_usage_bytes(blocks)
        span_start, span_end = app_flash_span(blocks)
        dynamic_size = align_up(used + args.slot_padding, args.slot_align)
        rows.append(
            {
                "path": str(path),
                "linkedFlashOffset": (span_start - XIP_BASE) if span_start >= XIP_BASE else None,
                "linkedFlashOffsetHex": f"0x{span_start - XIP_BASE:x}" if span_start >= XIP_BASE else None,
                "sizeBytes": used,
                "dynamicSlotBytes": dynamic_size,
                "dynamicSlotBytesHex": f"0x{dynamic_size:x}",
                "appRegionBytes": args.app_region_size,
                "fitsAppRegion": dynamic_size <= args.app_region_size,
                "recommendedFlashOffset": next_offset,
                "recommendedFlashOffsetHex": f"0x{next_offset:x}",
                "recommendedFitsAppRegion": next_offset + dynamic_size <= region_end,
            }
        )
        next_offset += dynamic_size
    print(json.dumps(rows, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description="Pico-Eurorack Bootloader/Apps helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    slot = subparsers.add_parser("slot-build", help="compile an Arduino sketch for an app slot")
    slot.add_argument("sketch", type=Path)
    slot.add_argument("--slot", type=int, required=True)
    slot.add_argument("--slot-size", type=int_arg, default=PICO_SELECTOR_DEFAULT_SLOT_BYTES)
    slot.add_argument("--slot-offset", type=int_arg)
    slot.add_argument("--first-app-offset", type=int_arg, default=PICO_SELECTOR_FIRST_APP_OFFSET)
    slot.add_argument("--fqbn", default=DEFAULT_FQBN)
    slot.add_argument("--build-path", type=Path, required=True)
    slot.add_argument("--library", action="append", default=[])
    slot.set_defaults(func=build_slot)

    pack = subparsers.add_parser("package", help="combine selector and slot-linked app UF2 files")
    pack.add_argument("--selector", type=Path, required=True)
    pack.add_argument("--app", type=Path, action="append", default=[])
    pack.add_argument(
        "--slot-app",
        type=slot_app_arg,
        action="append",
        default=[],
        metavar="SLOT=UF2",
        help="add a slot-linked UF2 at a physical selector slot",
    )
    pack.add_argument("--output", type=Path, required=True)
    pack.add_argument("--active", type=int, default=0)
    pack.add_argument("--target", choices=("rp2040", "rp2350"), default="rp2350")
    pack.add_argument("--cpu-hz", type=int_arg, default=DEFAULT_CPU_HZ)
    pack.add_argument("--slot-size", type=int_arg, default=PICO_SELECTOR_DEFAULT_SLOT_BYTES)
    pack.add_argument(
        "--fixed-slots",
        action="store_true",
        help="use legacy fixed slot offsets instead of reading each UF2's linked address",
    )
    pack.add_argument("--slot-padding", type=int_arg, default=DEFAULT_DYNAMIC_SLOT_PADDING)
    pack.add_argument("--slot-align", type=int_arg, default=FLASH_SECTOR_SIZE)
    pack.add_argument("--app-region-size", type=int_arg, default=PICO_SELECTOR_DEFAULT_APP_REGION_BYTES)
    pack.add_argument("--vector-offset", type=int_arg, default=PICO_SELECTOR_ARDUINO_VECTOR_OFFSET)
    pack.add_argument("--first-app-offset", type=int_arg, default=PICO_SELECTOR_FIRST_APP_OFFSET)
    pack.add_argument("--layout-json", type=Path)
    pack.set_defaults(func=package)

    measure_cmd = subparsers.add_parser("measure", help="report UF2 app flash usage")
    measure_cmd.add_argument("uf2", type=Path, nargs="+")
    measure_cmd.add_argument("--slot-size", type=int_arg, default=PICO_SELECTOR_DEFAULT_SLOT_BYTES)
    measure_cmd.add_argument("--slot-padding", type=int_arg, default=DEFAULT_DYNAMIC_SLOT_PADDING)
    measure_cmd.add_argument("--slot-align", type=int_arg, default=FLASH_SECTOR_SIZE)
    measure_cmd.add_argument("--first-app-offset", type=int_arg, default=PICO_SELECTOR_FIRST_APP_OFFSET)
    measure_cmd.add_argument("--app-region-size", type=int_arg, default=PICO_SELECTOR_DEFAULT_APP_REGION_BYTES)
    measure_cmd.set_defaults(func=measure)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
