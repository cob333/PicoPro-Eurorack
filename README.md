<p align="center">
  <img src="Web_images/Pico_Icon.jpg" alt="Pico Icon" width="560">
</p>

# PicoPro Eurorack

**English** | [简体中文](README_CN.md)

PicoPro Eurorack is an RP2350 multi-app DSP firmware and a derivative module based on 4HPico DSP. It packages several Arduino-Pico audio apps into one UF2. Apps are selected with the 64×32 OLED and rotary encoder.

Hardware project: [2HPico Eurorack Module Hardware](https://github.com/rheslip/2HPico-Eurorack-Module-Hardware)

Original project: [4HPico DSP Eurorack Module](https://github.com/rheslip/4HPico-DSP-Sketches)

## Panels

### 3U panels

| White | Black |
| :---: | :---: |
| <img src="Web_images/picopro_panel_white.jpg" alt="White PicoPro Eurorack panel" width="480"> | <img src="Web_images/picopro_panel_black.jpg" alt="Black PicoPro Eurorack panel" width="480"> |

### 1U panels

| White | Black |
| :---: | :---: |
| <img src="Web_images/picopro_1U_panel_white.jpg" alt="White PicoPro 1U panel" width="480"> | <img src="Web_images/picopro_1U_panel_black.jpg" alt="Black PicoPro 1U panel" width="480"> |

## Features

- Multiple independent DSP apps in one firmware image
- App switching through the boot selector
- Stereo audio, CV, Gate, OLED, and rotary encoder support
- Dynamic Flash slots sized for each app
- Browser simulator for menus, icons, and fonts
- Flexible external CV routing and per-parameter internal LFO modulation

## Apps

Apps and slot assignments are defined in [`Bootloader/apps.json`](Bootloader/apps.json).

| Slot | App | Status |
| ---: | --- | --- |
| 0 | Delay | Available |
| 1 | Reverb | Available |
| 2 | Tuner | Coming soon |
| 3 | Wavetable | Available |
| 4 | Grain | Available |
| 5 | Rings | Available |
| 6 | Acid | Coming soon |
| 7 | Calibration | Available |
| 8 | Glitch | Available |
| 9 | Flanger | Available |
| 10 | Ducking | Available |
| 11 | Crush | Available |

Tuner and Acid currently display `coming soon`. Reverb uses about 78% of global RAM, so it needs extra stability testing on hardware.

## Internal routing

![PicoPro internal modulation routing](Web_images/PicoPro_Routing.drawio.jpg)

Each parameter can use CV1, CV2, or an internal LFO as its modulation source. `Amount` sets the modulation depth. Every parameter has its own LFO with independent waveform and frequency settings.

## Requirements

The hardware requires an RP2350 with 4MB Flash. The full firmware builds at 250MHz by default.

Build tools:

- Arduino IDE 2.x or `arduino-cli`
- [Arduino-Pico](https://github.com/earlephilhower/arduino-pico), tested with version `5.5.0`
- Python 3 and [Pillow](https://python-pillow.org/)
- CMake 3.13 or newer
- Ninja, recommended
- Pico SDK, optionally set through `PICO_SDK_PATH`

Arduino libraries:

- Adafruit GFX Library
- Adafruit SSD1306
- pico-audio
- [DaisySP_Teensy](https://github.com/rheslip/DaisySP_Teensy) for Reverb
- RINGS and STMLIB from [arduinoMI](https://github.com/poetaster/arduinoMI)

Arduino-Pico provides `I2S`, `Wire`, and `SPI`. The build script loads the project library from `lib/PicoProlib` automatically.

## Build

Run this command from the repository root:

```sh
python3 Bootloader/Tools/build_slots.py --package --clean
```

The script compiles every app and the boot selector, then creates:

```text
Bootloader/build/PicoPro_Firmware.uf2
Bootloader/build/PicoPro_Firmware.layout.json
Bootloader/build/slots/*.uf2
```

If the Pico SDK is not in its default location:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
python3 Bootloader/Tools/build_slots.py --package --clean
```

For other options:

```sh
python3 Bootloader/Tools/build_slots.py --help
```

## Flash and use

1. Put the RP2350 into BOOTSEL mode and connect it to your computer.
2. Copy `Bootloader/build/PicoPro_Firmware.uf2` to the RP2350 USB drive.
3. After rebooting, turn the encoder to choose an app and press it to enter.
4. Hold the encoder for about 1.8 seconds inside an app to return to the selector.

Confirm that the target hardware uses an RP2350 with 4MB Flash before flashing.

## UI simulator

```sh
python3 sims/serve.py
```

Open `http://127.0.0.1:8765/sims/index.html` in a browser.

- `←` / `→`: rotate the encoder
- `↓`: press the encoder
- Hold `↓`: return to the selector
- `1` to `8`: open an app directly
- `S`: return to the selector
- `R`: reset the current view

The simulator reads the real app manifest, selector icons, and `Fonts/*.h`. See [`sims/README.md`](sims/README.md) for details.

## Repository layout

```text
Apps/                  Arduino DSP apps
Bootloader/            Selector, configuration, and build tools
Fonts/                 OLED fonts
Images/                App icon animation frames
Tools/                 Audio asset conversion tools
Web_images/            Documentation images
lib/PicoProlib/        Shared PicoPro library
sims/                  UI simulators
```

`Bootloader/build/` and `reference/` are local-only directories and are not committed to Git.

## Add an app

Use this directory structure:

```text
Apps/MyApp/MyApp.ino
```

Icons are 64×32 PNG files:

```text
Images/my_app_icon/my_app_icon-1.png
Images/my_app_icon/my_app_icon-2.png
```

Preview and register the app:

```sh
python3 Bootloader/Tools/register_placeholder_apps.py MyApp --dry-run
python3 Bootloader/Tools/register_placeholder_apps.py MyApp --require-icons
```

Rebuild the full firmware after registration.

## Update audio assets

Grain samples are stored in `Apps/Grain/Samples/`:

```sh
python3 Tools/import_grain_samples.py
```

Wavetable source files are stored in `Apps/Wavetable/waves/`:

```sh
python3 Tools/import_wavetables.py --help
```

The scripts generate the converted `.h` files. Do not edit those files by hand.

## Credits

- [Rich Heslip](https://github.com/rheslip): PicoPro hardware, original firmware, and DSP sketches
- [Earle F. Philhower, III and contributors](https://github.com/earlephilhower/arduino-pico): Arduino-Pico
- [Electro-Smith](https://github.com/electro-smith/DaisySP): DaisySP DSP library
- [Emilie Gillet / Mutable Instruments](https://github.com/pichenettes/eurorack): original Rings and STMLIB code
- [Mark Washeim](https://github.com/poetaster/arduinoMI): Mutable Instruments Arduino ports
- [Adafruit](https://github.com/adafruit): Adafruit GFX and SSD1306 libraries

Upstream projects retain their original copyrights and licenses. Check the license terms of all libraries before distributing the firmware or source code.
