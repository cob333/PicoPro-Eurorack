#!/usr/bin/env python3
"""Build PicoPro apps as bootloader slot UF2 files."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
BOOT_DIR = ROOT / "Bootloader"
BOOT_TOOL = BOOT_DIR / "Tools" / "pico_boot_apps.py"
DEFAULT_BUILD_DIR = BOOT_DIR / "build" / "slots"
DEFAULT_SELECTOR_BUILD_DIR = BOOT_DIR / "build" / "selector"
DEFAULT_OUTPUT = BOOT_DIR / "build" / "PicoPro_Firmware.uf2"
DEFAULT_CPU_HZ = 250_000_000
DEFAULT_FQBN = "rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=250"
DEFAULT_FLASH_BYTES = 4 * 1024 * 1024
DEFAULT_FIRST_APP_OFFSET = 0x40000
DEFAULT_APP_STATE_BYTES = 160 * 1024
DEFAULT_SLOT_ALIGN = 4096
DEFAULT_SLOT_PADDING = 0


@dataclass(frozen=True)
class App:
  id: str
  name: str
  slot: int
  sketch: Path


@dataclass(frozen=True)
class BuiltApp:
  app: App
  uf2: Path
  flash_offset: int
  used_size: int
  allocated_size: int


def load_boot_tool():
  spec = importlib.util.spec_from_file_location("pico_boot_apps", BOOT_TOOL)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {BOOT_TOOL}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


BOOT = load_boot_tool()


def int_arg(value: Any) -> int:
  if isinstance(value, int):
    return value
  text = str(value).strip().lower()
  if text.endswith("k"):
    return int(text[:-1], 0) * 1024
  if text.endswith("m"):
    return int(text[:-1], 0) * 1024 * 1024
  return int(text, 0)


def load_manifest(path: Path) -> dict[str, Any]:
  data = json.loads(path.read_text())
  slots: set[int] = set()
  for item in data.get("apps", []):
    slot = int_arg(item["slot"])
    if slot in slots:
      raise ValueError(f"slot {slot} is declared more than once")
    slots.add(slot)
  return data


def apps_from_manifest(data: dict[str, Any]) -> list[App]:
  apps: list[App] = []
  for item in data.get("apps", []):
    apps.append(
      App(
        id=str(item["id"]),
        name=str(item["name"]),
        slot=int_arg(item["slot"]),
        sketch=ROOT / str(item["sketch"]),
      )
    )
  if not apps:
    raise ValueError("manifest does not contain any apps")
  return sorted(apps, key=lambda app: app.slot)


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
  print("+", " ".join(cmd), flush=True)
  subprocess.run(cmd, check=True, env=env)


def c_string(value: str) -> str:
  return json.dumps(value)


def generate_selector_apps(apps: list[App]) -> None:
  count = max(app.slot for app in apps) + 1
  names = ["Empty"] * count
  for app in apps:
    names[app.slot] = app.name
  lines = [
    "#ifndef PICOPRO_SELECTOR_APPS_H_",
    "#define PICOPRO_SELECTOR_APPS_H_",
    "",
    "static const char *const PICOPRO_SELECTOR_APP_NAMES[] = {",
  ]
  for name in names:
    lines.append(f"  {c_string(name)},")
  lines.extend(
    [
      "};",
      "",
      f"#define PICOPRO_SELECTOR_APP_NAME_COUNT {len(names)}u",
      "",
      "#endif // PICOPRO_SELECTOR_APPS_H_",
      "",
    ]
  )
  (BOOT_DIR / "Selector" / "selector_apps.h").write_text("\n".join(lines))


def frame_index(path: Path) -> int:
  match = re.search(r"-(\d+)\.png$", path.name)
  if not match:
    raise ValueError(f"icon frame name must end with -NUMBER.png: {path}")
  return int(match.group(1))


def image_to_oled_frame(path: Path, *, width: int = 64, height: int = 32) -> list[int]:
  try:
    from PIL import Image
  except ImportError as exc:
    raise RuntimeError("Pillow is required to convert selector icon PNG frames") from exc

  image = Image.open(path).convert("RGBA")
  if image.size != (width, height):
    raise ValueError(f"{path} is {image.size}, expected {(width, height)}")

  pages = height // 8
  frame: list[int] = []
  for page in range(pages):
    for x in range(width):
      byte = 0
      for bit in range(8):
        _r, _g, _b, alpha = image.getpixel((x, page * 8 + bit))
        if alpha >= 128:
          byte |= 1 << bit
      frame.append(byte)
  return frame


def c_identifier(value: str) -> str:
  ident = re.sub(r"[^A-Za-z0-9_]", "_", value).upper()
  if not ident or ident[0].isdigit():
    ident = f"APP_{ident}"
  return ident


def generate_selector_app_icons(apps: list[App]) -> None:
  count = max(app.slot for app in apps) + 1
  frame_sets: dict[int, tuple[str, list[list[int]]]] = {}

  for app in apps:
    icon_dir = ROOT / "Images" / f"{app.id}_icon"
    if not icon_dir.exists():
      continue
    frames = sorted(icon_dir.glob("*.png"), key=frame_index)
    if not frames:
      continue
    frame_sets[app.slot] = (
      c_identifier(app.id),
      [image_to_oled_frame(frame) for frame in frames],
    )

  lines = [
    "#ifndef PICOPRO_SELECTOR_APP_ICONS_H_",
    "#define PICOPRO_SELECTOR_APP_ICONS_H_",
    "",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    "#define PICOPRO_SELECTOR_ICON_FRAME_BYTES 256u",
    "",
    "typedef struct {",
    "  const uint8_t (*frames)[PICOPRO_SELECTOR_ICON_FRAME_BYTES];",
    "  uint8_t frame_count;",
    "} PicoProSelectorIcon;",
    "",
  ]

  for _slot, (ident, frames) in sorted(frame_sets.items()):
    lines.append(
      f"static const uint8_t PICOPRO_SELECTOR_{ident}_ICON_FRAMES[]"
      f"[PICOPRO_SELECTOR_ICON_FRAME_BYTES] = {{"
    )
    for index, frame in enumerate(frames):
      lines.append(f"  {{ // frame {index + 1}")
      for row in range(0, len(frame), 16):
        chunk = frame[row : row + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
      lines.append("  },")
    lines.append("};")
    lines.append("")

  lines.append("static const PicoProSelectorIcon PICOPRO_SELECTOR_APP_ICONS[] = {")
  for slot in range(count):
    if slot in frame_sets:
      ident, frames = frame_sets[slot]
      lines.append(
        f"  {{ PICOPRO_SELECTOR_{ident}_ICON_FRAMES, {len(frames)}u }},"
      )
    else:
      lines.append("  { NULL, 0u },")
  lines.extend(
    [
      "};",
      "",
      f"#define PICOPRO_SELECTOR_APP_ICON_COUNT {count}u",
      "",
      "#endif // PICOPRO_SELECTOR_APP_ICONS_H_",
      "",
    ]
  )
  (BOOT_DIR / "Selector" / "selector_app_icons.h").write_text("\n".join(lines))


def build_selector(build_dir: Path, *, clean: bool) -> Path:
  if clean and build_dir.exists():
    shutil.rmtree(build_dir)
  build_dir.mkdir(parents=True, exist_ok=True)

  toolchain = Path.home() / "Library/Arduino15/packages/rp2040/tools/pqt-gcc/4.1.0-1aec55e/bin"
  env = os.environ.copy()
  if toolchain.exists():
    env["PATH"] = f"{toolchain}{os.pathsep}{env.get('PATH', '')}"

  cmake_cmd = [
    "cmake",
    "-S",
    str(BOOT_DIR / "Selector"),
    "-B",
    str(build_dir),
    "-DPICO_BOARD=pico2",
  ]
  local_sdk = Path.home() / "Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk"
  if "PICO_SDK_PATH" not in os.environ and (local_sdk / "external/pico_sdk_import.cmake").exists():
    cmake_cmd.append(f"-DPICO_SDK_PATH={local_sdk}")
  if toolchain.exists():
    cmake_cmd.extend(
      [
        f"-DCMAKE_C_COMPILER={toolchain / 'arm-none-eabi-gcc'}",
        f"-DCMAKE_CXX_COMPILER={toolchain / 'arm-none-eabi-g++'}",
      ]
    )
  if shutil.which("ninja"):
    cmake_cmd.extend(["-G", "Ninja"])

  if not (build_dir / "build.ninja").exists() and not (build_dir / "Makefile").exists():
    run(cmake_cmd, env=env)
  run(["cmake", "--build", str(build_dir), "--target", "picopro_selector"], env=env)

  candidates = [
    build_dir / "PicoPro_Firmware.uf2",
    build_dir / "PicoPro_selector.uf2",
  ]
  for path in candidates:
    if path.exists():
      return path
  return find_uf2(build_dir)


def find_uf2(build_path: Path) -> Path:
  matches = sorted(build_path.rglob("*.uf2"), key=lambda path: path.stat().st_mtime_ns)
  if not matches:
    raise FileNotFoundError(f"no UF2 file found under {build_path}")
  return matches[-1]


def slot_build(
  app: App,
  *,
  fqbn: str,
  flash_offset: int,
  flash_length: int,
  build_root: Path,
  libraries: list[Path],
  clean: bool,
) -> Path:
  if not app.sketch.exists():
    raise FileNotFoundError(f"{app.name} sketch does not exist: {app.sketch}")

  build_path = build_root / f"slot_{app.slot}_{app.id}"
  if clean and build_path.exists():
    shutil.rmtree(build_path)

  cmd = [
    sys.executable,
    str(BOOT_TOOL),
    "slot-build",
    str(app.sketch),
    "--slot",
    str(app.slot),
    "--slot-size",
    str(flash_length),
    "--slot-offset",
    str(flash_offset),
    "--fqbn",
    fqbn,
    "--build-path",
    str(build_path),
  ]
  for library in libraries:
    cmd.extend(["--library", str(library)])
  run(cmd)

  uf2 = find_uf2(build_path)
  out = build_root / f"{app.slot:02d}_{app.id}.uf2"
  out.write_bytes(uf2.read_bytes())
  return out


def align_up(value: int, alignment: int) -> int:
  if alignment <= 0:
    raise ValueError("alignment must be greater than zero")
  return ((value + alignment - 1) // alignment) * alignment


def uf2_usage(path: Path) -> tuple[int, int]:
  blocks = BOOT.read_uf2(path)
  span_start, span_end = BOOT.app_flash_span(blocks)
  if span_start < BOOT.XIP_BASE:
    raise ValueError(f"{path} does not contain flash payload blocks")
  return span_start - BOOT.XIP_BASE, span_end - span_start


def build_dynamic_slots(
  apps: list[App],
  *,
  fqbn: str,
  first_app_offset: int,
  app_region_size: int,
  slot_align: int,
  slot_padding: int,
  build_root: Path,
  libraries: list[Path],
  clean: bool,
) -> list[BuiltApp]:
  built: list[BuiltApp] = []
  next_offset = first_app_offset
  region_end = first_app_offset + app_region_size

  for app in apps:
    remaining = region_end - next_offset
    if remaining <= 0:
      raise ValueError(f"no flash space left before building {app.name}")

    uf2 = slot_build(
      app,
      fqbn=fqbn,
      flash_offset=next_offset,
      flash_length=remaining,
      build_root=build_root,
      libraries=libraries,
      clean=clean,
    )
    linked_offset, used_size = uf2_usage(uf2)
    if linked_offset != next_offset:
      raise ValueError(
        f"{app.name} linked at 0x{linked_offset:x}, expected 0x{next_offset:x}"
      )
    allocated_size = align_up(used_size + slot_padding, slot_align)
    if linked_offset + allocated_size > region_end:
      raise ValueError(
        f"{app.name} needs 0x{allocated_size:x} bytes and exceeds app region end 0x{region_end:x}"
      )
    built.append(
      BuiltApp(
        app=app,
        uf2=uf2,
        flash_offset=linked_offset,
        used_size=used_size,
        allocated_size=allocated_size,
      )
    )
    next_offset += allocated_size

  return built


def package(
  built_apps: list[BuiltApp],
  *,
  selector: Path,
  output: Path,
  target: str,
  cpu_hz: int,
  first_app_offset: int,
  app_region_size: int,
  slot_align: int,
  slot_padding: int,
  active: int,
) -> None:
  cmd = [
    sys.executable,
    str(BOOT_TOOL),
    "package",
    "--selector",
    str(selector),
    "--output",
    str(output),
    "--target",
    target,
    "--cpu-hz",
    str(cpu_hz),
    "--first-app-offset",
    str(first_app_offset),
    "--app-region-size",
    str(app_region_size),
    "--slot-align",
    str(slot_align),
    "--slot-padding",
    str(slot_padding),
    "--active",
    str(active),
    "--layout-json",
    str(output.with_suffix(".layout.json")),
  ]
  for built in built_apps:
    cmd.extend(["--slot-app", f"{built.app.slot}={built.uf2}"])
  run(cmd)


def main() -> None:
  parser = argparse.ArgumentParser(description="Build PicoPro bootloader app slots")
  parser.add_argument("--manifest", type=Path, default=BOOT_DIR / "apps.json")
  parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_DIR)
  parser.add_argument("--selector", type=Path, help="selector UF2 to package with slot apps")
  parser.add_argument("--selector-build-root", type=Path, default=DEFAULT_SELECTOR_BUILD_DIR)
  parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
  parser.add_argument("--package", action="store_true", help="package slot UF2 files with --selector")
  parser.add_argument("--active", type=int, default=0)
  parser.add_argument("--fqbn", default=None, help="Arduino FQBN for app slots; defaults to 250MHz Pico 2")
  parser.add_argument("--cpu-hz", type=int_arg, default=None, help="runtime CPU clock stored in boot config")
  parser.add_argument("--clean", action="store_true", help="remove per-slot build folders first")
  parser.add_argument("--library", action="append", type=Path, default=[ROOT / "lib"])
  args = parser.parse_args()

  manifest = load_manifest(args.manifest)
  apps = apps_from_manifest(manifest)
  generate_selector_apps(apps)
  generate_selector_app_icons(apps)
  fqbn = str(args.fqbn or manifest.get("fqbn", DEFAULT_FQBN))
  target = str(manifest.get("target", "rp2350"))
  cpu_hz = int_arg(args.cpu_hz if args.cpu_hz is not None else manifest.get("cpuHz", DEFAULT_CPU_HZ))
  first_app_offset = int_arg(manifest.get("firstAppOffset", DEFAULT_FIRST_APP_OFFSET))
  flash_bytes = int_arg(manifest.get("flashBytes", DEFAULT_FLASH_BYTES))
  app_state_bytes = int_arg(manifest.get("appStateBytes", DEFAULT_APP_STATE_BYTES))
  if "appRegionSize" in manifest:
    app_region_size = int_arg(manifest["appRegionSize"])
  else:
    app_region_size = flash_bytes - first_app_offset - app_state_bytes
  slot_align = int_arg(manifest.get("slotAlign", DEFAULT_SLOT_ALIGN))
  slot_padding = int_arg(manifest.get("slotPadding", DEFAULT_SLOT_PADDING))

  args.build_root.mkdir(parents=True, exist_ok=True)
  built_apps = build_dynamic_slots(
    apps,
    fqbn=fqbn,
    first_app_offset=first_app_offset,
    app_region_size=app_region_size,
    slot_align=slot_align,
    slot_padding=slot_padding,
    build_root=args.build_root,
    libraries=args.library,
    clean=args.clean,
  )

  print("\nBuilt dynamic slot UF2 files:")
  for built in built_apps:
    print(
      f"  slot {built.app.slot}: {built.app.name} -> {built.uf2} "
      f"@ 0x{built.flash_offset:x}, used 0x{built.used_size:x}, alloc 0x{built.allocated_size:x}"
    )

  if args.package:
    selector = args.selector
    if selector is None:
      selector = build_selector(args.selector_build_root, clean=args.clean)
    package(
      built_apps,
      selector=selector,
      output=args.output,
      target=target,
      cpu_hz=cpu_hz,
      first_app_offset=first_app_offset,
      app_region_size=app_region_size,
      slot_align=slot_align,
      slot_padding=slot_padding,
      active=args.active,
    )
    print(f"\nPackaged firmware: {args.output}")


if __name__ == "__main__":
  main()
