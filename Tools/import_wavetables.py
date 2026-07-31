#!/usr/bin/env python3
"""Convert 16-bit mono WAV wavetable banks into a tiny C header.

Input WAV files are treated as one or more single-cycle tables. By default each
source table is 2048 samples. The firmware does no WAV parsing and keeps all
tables in flash.
"""

from __future__ import annotations

import argparse
import math
import re
import struct
import wave
from pathlib import Path


def symbol(name: str) -> str:
  ident = re.sub(r"[^A-Za-z0-9]+", "_", Path(name).stem).strip("_").lower()
  return ident or "bank"


def display_name(name: str) -> str:
  return Path(name).stem[:8].lower() or "bank"


def read_mono_i16(path: Path) -> tuple[int, list[int]]:
  with wave.open(str(path), "rb") as wav:
    channels = wav.getnchannels()
    width = wav.getsampwidth()
    rate = wav.getframerate()
    frames = wav.getnframes()
    if width != 2:
      raise ValueError(f"{path}: expected 16-bit PCM WAV, got {width * 8}-bit")
    raw = wav.readframes(frames)

  values = struct.unpack("<" + "h" * (len(raw) // 2), raw)
  if channels == 1:
    return rate, list(values)

  mono: list[int] = []
  for i in range(0, len(values), channels):
    mono.append(int(sum(values[i:i + channels]) / channels))
  return rate, mono


def remove_dc(samples: list[int]) -> list[float]:
  if not samples:
    return []
  dc = sum(samples) / len(samples)
  return [x - dc for x in samples]


def normalize(samples: list[float]) -> list[float]:
  peak = max((abs(x) for x in samples), default=0.0)
  if peak <= 0.0:
    return [0.0 for _ in samples]
  scale = 32767.0 / peak
  return [max(-32768.0, min(32767.0, x * scale)) for x in samples]


def bandlimited_resample_cycle(samples: list[float],
                               output_size: int,
                               max_harmonic: int) -> list[int]:
  size = len(samples)
  if size < 2:
    return [0] * output_size

  max_harmonic = max(1, min(max_harmonic, (size // 2) - 1, (output_size // 2) - 1))
  coeffs: list[complex] = []
  for harmonic in range(max_harmonic + 1):
    acc = 0j
    for n, sample in enumerate(samples):
      phase = -2.0 * math.pi * harmonic * n / size
      acc += sample * complex(math.cos(phase), math.sin(phase))
    coeffs.append(acc / size)

  result: list[int] = []
  for n in range(output_size):
    value = coeffs[0].real
    for harmonic in range(1, max_harmonic + 1):
      phase = 2.0 * math.pi * harmonic * n / output_size
      value += 2.0 * (coeffs[harmonic].real * math.cos(phase) -
                      coeffs[harmonic].imag * math.sin(phase))
    result.append(int(round(max(-32768.0, min(32767.0, value)))))
  return result


def chunks(samples: list[int], source_size: int) -> list[list[int]]:
  count = len(samples) // source_size
  return [samples[i * source_size:(i + 1) * source_size] for i in range(count)]


def parse_int_list(text: str, name: str) -> list[int]:
  values = [int(x) for x in text.split(",") if x.strip()]
  if not values:
    raise ValueError(f"{name} must contain at least one value")
  return values


def log2_power_of_two(value: int) -> int:
  if value <= 1 or value & (value - 1):
    raise ValueError("mip sizes must be powers of two")
  return value.bit_length() - 1


def format_table(values: list[int]) -> str:
  rows = []
  for i in range(0, len(values), 8):
    rows.append("    " + ", ".join(f"{v:6d}" for v in values[i:i + 8]))
  return ",\n".join(rows)


def main() -> None:
  parser = argparse.ArgumentParser(description="Generate PicoPro wavetable_bank.h")
  parser.add_argument("waves", nargs="+", type=Path, help="16-bit WAV files")
  parser.add_argument("--source-size", type=int, default=2048,
                      help="samples per source wavetable cycle")
  parser.add_argument("--size", type=int, default=512,
                      help="samples for the first/highest-quality firmware mip")
  parser.add_argument("--mip-harmonics", default="128,64,32,16,8",
                      help="comma-separated harmonic caps for mip levels")
  parser.add_argument("--mip-sizes", default="512,256,128,64,32",
                      help="comma-separated table sizes for mip levels")
  parser.add_argument("--out", type=Path,
                      default=Path("Apps/Wavetable/wavetables/wavetable_bank.h"))
  args = parser.parse_args()
  mip_harmonics = parse_int_list(args.mip_harmonics, "--mip-harmonics")
  mip_sizes = parse_int_list(args.mip_sizes, "--mip-sizes")
  if len(mip_sizes) != len(mip_harmonics):
    raise ValueError("--mip-sizes and --mip-harmonics must have the same length")
  if mip_sizes[0] != args.size:
    raise ValueError("--size must match the first --mip-sizes value")
  mip_index_shifts = [32 - log2_power_of_two(size) for size in mip_sizes]
  mip_frac_shifts = [shift - 16 for shift in mip_index_shifts]
  mip_masks = [size - 1 for size in mip_sizes]

  banks = []
  tables: list[list[list[int]]] = []
  for path in args.waves:
    _rate, samples = read_mono_i16(path)
    source_tables = chunks(samples, args.source_size)
    if not source_tables:
      raise ValueError(f"{path}: not enough samples for one {args.source_size}-sample table")
    start = len(tables)
    for source in source_tables:
      cycle = normalize(remove_dc(source))
      tables.append([bandlimited_resample_cycle(cycle, mip_size, harmonic)
                     for mip_size, harmonic in zip(mip_sizes, mip_harmonics)])
    banks.append({
        "symbol": symbol(path.name),
        "name": display_name(path.name),
        "start": start,
        "count": len(source_tables),
    })

  args.out.parent.mkdir(parents=True, exist_ok=True)
  with args.out.open("w") as out:
    out.write("#pragma once\n")
    out.write("#include <stdint.h>\n\n")
    out.write("// Generated by Tools/import_wavetables.py. Do not edit by hand.\n")
    out.write(f"#define PICOPRO_WAVETABLE_SIZE {args.size}\n")
    out.write(f"#define PICOPRO_WAVETABLE_COUNT {len(tables)}\n")
    out.write(f"#define PICOPRO_WAVETABLE_MIP_COUNT {len(mip_harmonics)}\n")
    out.write("#define PICOPRO_WAVETABLE_VARIABLE_MIP_SIZE 1\n")
    out.write(f"#define PICOPRO_WAVETABLE_BANK_COUNT {len(banks)}\n\n")
    for mip, size in enumerate(mip_sizes):
      out.write(f"#define PICOPRO_WAVETABLE_MIP{mip}_SIZE {size}\n")
      out.write(f"#define PICOPRO_WAVETABLE_MIP{mip}_INDEX_SHIFT {mip_index_shifts[mip]}\n")
      out.write(f"#define PICOPRO_WAVETABLE_MIP{mip}_FRAC_SHIFT {mip_frac_shifts[mip]}\n")
      out.write(f"#define PICOPRO_WAVETABLE_MIP{mip}_MASK {mip_masks[mip]}\n")
    out.write("\n")
    out.write("struct PicoProWavetableBank {\n")
    out.write("  const char *name;\n")
    out.write("  uint16_t start;\n")
    out.write("  uint16_t count;\n")
    out.write("};\n\n")
    out.write("static const PicoProWavetableBank PICOPRO_WAVETABLE_BANKS[] = {\n")
    for bank in banks:
      out.write(f'  {{"{bank["name"]}", {bank["start"]}, {bank["count"]}}},\n')
    out.write("};\n\n")
    out.write("static const char *PICOPRO_WAVETABLE_BANK_NAMES[] = {\n")
    for bank in banks:
      out.write(f'  "{bank["name"]}",\n')
    out.write("};\n\n")
    for mip, size in enumerate(mip_sizes):
      out.write(f"static const int16_t PICOPRO_WAVETABLES_MIP{mip}")
      out.write(f"[PICOPRO_WAVETABLE_COUNT][PICOPRO_WAVETABLE_MIP{mip}_SIZE] = {{\n")
      for mips in tables:
        out.write("    {\n")
        out.write(format_table(mips[mip]).replace("    ", "      "))
        out.write("\n    },\n")
      out.write("};\n\n")

    out.write("static inline const int16_t *PicoProWavetableTable(uint16_t table_index, uint8_t mip_index) {\n")
    out.write("  switch (mip_index) {\n")
    for mip in range(len(mip_sizes)):
      out.write(f"    case {mip}: return PICOPRO_WAVETABLES_MIP{mip}[table_index];\n")
    out.write("    default: return PICOPRO_WAVETABLES_MIP0[table_index];\n")
    out.write("  }\n")
    out.write("}\n\n")
    out.write("static inline uint8_t PicoProWavetableMipIndexShift(uint8_t mip_index) {\n")
    out.write("  switch (mip_index) {\n")
    for mip, shift in enumerate(mip_index_shifts):
      out.write(f"    case {mip}: return {shift};\n")
    out.write(f"    default: return {mip_index_shifts[0]};\n")
    out.write("  }\n")
    out.write("}\n\n")
    out.write("static inline uint8_t PicoProWavetableMipFracShift(uint8_t mip_index) {\n")
    out.write("  switch (mip_index) {\n")
    for mip, shift in enumerate(mip_frac_shifts):
      out.write(f"    case {mip}: return {shift};\n")
    out.write(f"    default: return {mip_frac_shifts[0]};\n")
    out.write("  }\n")
    out.write("}\n\n")
    out.write("static inline uint16_t PicoProWavetableMipMask(uint8_t mip_index) {\n")
    out.write("  switch (mip_index) {\n")
    for mip, mask in enumerate(mip_masks):
      out.write(f"    case {mip}: return {mask};\n")
    out.write(f"    default: return {mip_masks[0]};\n")
    out.write("  }\n")
    out.write("}\n")

  print(f"wrote {args.out} ({len(banks)} banks, {len(tables)} tables, "
        f"{len(mip_harmonics)} mips, {sum(mip_sizes)} samples/table set)")


if __name__ == "__main__":
  main()
