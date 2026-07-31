#!/usr/bin/env python3
"""Register new Apps/* folders as lightweight PicoPro placeholder apps.

The script is intentionally small and build-flow friendly:

- scans Apps/ for directories not present in Bootloader/apps.json
- creates a minimal placeholder sketch when the app has no .ino file
- assigns the next free selector slot
- refreshes selector_apps.h and selector_app_icons.h via build_slots.py helpers

Icon convention is the existing selector convention:

  Images/{app_id}_icon/*.png

Missing icon folders are reported as warnings so app registration never
silently fails just because artwork is still in progress.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
BOOT_DIR = ROOT / "Bootloader"
APPS_DIR = ROOT / "Apps"
IMAGES_DIR = ROOT / "Images"
MANIFEST = BOOT_DIR / "apps.json"
BUILD_SLOTS = BOOT_DIR / "Tools" / "build_slots.py"


def load_build_slots():
  spec = importlib.util.spec_from_file_location("build_slots", BUILD_SLOTS)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {BUILD_SLOTS}")
  module = importlib.util.module_from_spec(spec)
  sys.modules[spec.name] = module
  spec.loader.exec_module(module)
  return module


def load_manifest(path: Path) -> dict[str, Any]:
  return json.loads(path.read_text())


def write_manifest(path: Path, data: dict[str, Any]) -> None:
  path.write_text(json.dumps(data, indent=2) + "\n")


def app_id_from_dir(name: str) -> str:
  spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
  ident = re.sub(r"[^A-Za-z0-9]+", "_", spaced).strip("_").lower()
  if not ident:
    raise ValueError(f"cannot derive app id from {name!r}")
  return ident


def display_name_from_dir(name: str) -> str:
  text = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name)
  text = re.sub(r"[_-]+", " ", text).strip()
  return text or name


def c_string(value: str) -> str:
  return json.dumps(value)


def placeholder_sketch(app_name: str) -> str:
  return (
      '#include "PlaceholderApp.h"\n'
      "\n"
      "void setup() {\n"
      f"  PicoProPlaceholderSetup({c_string(app_name)});\n"
      "}\n"
      "\n"
      "void loop() {\n"
      "  PicoProPlaceholderLoop();\n"
      "}\n"
  )


def app_dirs(selected: list[str] | None) -> list[Path]:
  if selected:
    dirs = [APPS_DIR / name for name in selected]
  else:
    dirs = [path for path in APPS_DIR.iterdir() if path.is_dir() and not path.name.startswith(".")]
  return sorted(dirs, key=lambda path: path.name.lower())


def next_free_slot(used: set[int]) -> int:
  slot = 0
  while slot in used:
    slot += 1
  return slot


def main() -> None:
  parser = argparse.ArgumentParser(
      description="Register new Apps folders as PicoPro placeholder selector apps"
  )
  parser.add_argument(
      "apps",
      nargs="*",
      help="optional Apps directory names to register; default scans all Apps/*",
  )
  parser.add_argument("--manifest", type=Path, default=MANIFEST)
  parser.add_argument("--dry-run", action="store_true", help="show planned changes only")
  parser.add_argument(
      "--force-placeholder",
      action="store_true",
      help="overwrite/create Apps/<Name>/<Name>.ino with PicoProPlaceholderSetup",
  )
  parser.add_argument(
      "--require-icons",
      action="store_true",
      help="fail if Images/{app_id}_icon is missing",
  )
  args = parser.parse_args()

  manifest = load_manifest(args.manifest)
  apps = manifest.setdefault("apps", [])
  registered_paths = {str(item.get("sketch", "")).rstrip("/") for item in apps}
  registered_ids = {str(item.get("id", "")) for item in apps}
  used_slots = {int(item["slot"]) for item in apps}
  planned: list[tuple[Path, dict[str, Any], Path | None, bool]] = []
  warnings: list[str] = []

  for directory in app_dirs(args.apps or None):
    if not directory.exists() or not directory.is_dir():
      raise FileNotFoundError(f"app directory does not exist: {directory}")

    sketch_path = f"Apps/{directory.name}"
    app_id = app_id_from_dir(directory.name)
    if sketch_path in registered_paths or app_id in registered_ids:
      continue

    icon_dir = IMAGES_DIR / f"{app_id}_icon"
    if not icon_dir.exists():
      message = f"missing selector icon folder: {icon_dir.relative_to(ROOT)}"
      if args.require_icons:
        raise FileNotFoundError(message)
      warnings.append(message)

    sketch_file: Path | None = None
    will_create_placeholder = False
    app_name = display_name_from_dir(directory.name)
    ino_files = sorted(directory.glob("*.ino"))
    if args.force_placeholder or not ino_files:
      sketch_file = directory / f"{directory.name}.ino"
      will_create_placeholder = True

    entry = {
        "id": app_id,
        "name": app_name,
        "slot": next_free_slot(used_slots),
        "sketch": sketch_path,
    }
    used_slots.add(entry["slot"])
    registered_paths.add(sketch_path)
    registered_ids.add(app_id)
    planned.append((directory, entry, sketch_file, will_create_placeholder))

  if not planned:
    print("No new Apps directories to register.")
    for warning in warnings:
      print(f"warning: {warning}", file=sys.stderr)
    return

  for directory, entry, sketch_file, will_create_placeholder in planned:
    action = "write placeholder" if will_create_placeholder else "keep existing sketch"
    print(
        f"slot {entry['slot']}: {entry['name']} ({entry['id']}) "
        f"from {directory.relative_to(ROOT)} - {action}"
    )
    if sketch_file is not None:
      print(f"  sketch: {sketch_file.relative_to(ROOT)}")

  for warning in warnings:
    print(f"warning: {warning}", file=sys.stderr)

  if args.dry_run:
    print("Dry run only; no files changed.")
    return

  for _directory, entry, sketch_file, will_create_placeholder in planned:
    if will_create_placeholder and sketch_file is not None:
      sketch_file.write_text(placeholder_sketch(entry["name"]))
    apps.append(entry)

  apps.sort(key=lambda item: int(item["slot"]))
  write_manifest(args.manifest, manifest)

  build_slots = load_build_slots()
  parsed_apps = build_slots.apps_from_manifest(manifest)
  build_slots.generate_selector_apps(parsed_apps)
  build_slots.generate_selector_app_icons(parsed_apps)
  print(f"Updated {args.manifest.relative_to(ROOT)}")
  print("Updated Bootloader/Selector/selector_apps.h")
  print("Updated Bootloader/Selector/selector_app_icons.h")


if __name__ == "__main__":
  main()
