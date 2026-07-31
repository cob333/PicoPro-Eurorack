# PicoPro UI Simulator

Tiny desktop simulator for the PicoPro 64×32 OLED selector and app UI. It uses
the same Adafruit GFX built-in 5×7 font as the firmware, reads
`Bootloader/apps.json`, and parses the generated selector icon frames from
`Bootloader/Selector/selector_app_icons.h`.
The display is rendered as a black OLED panel with solid white pixels and no
grid lines between pixels.

Recommended browser simulator, no Python GUI dependencies:

```sh
python3 sims/serve.py
```

It serves the repository at `http://127.0.0.1:8765/sims/index.html`. Font,
registered apps, and selector icons are loaded from the repo on page refresh.
The browser page also previews the current firmware font beside every
`Fonts/*.h` Adafruit GFX font header, using the same 64×32 OLED renderer for
quick UI readability comparisons.

Controls:

- `Left`: encoder rotate left
- `Right`: encoder rotate right
- `Down` in selector: enter selected app
- `Down` in app: encoder switch press
- hold `Down` in app for about 1.8 s: return to selector
- `1`...`8`: jump directly into the corresponding registered app
- `S`: return to selector
- `R`: reset current demo menu
- `Screen scale`: adjust the simulated OLED's overall size

Delay, Reverb, and Wavetable use dedicated simulations. Placeholder apps show their app name
centered, matching the current placeholder firmware sketches. The selector uses
the same generated bitmap icon frames as the firmware.

There is also an optional Tkinter version:

```sh
python3 sims/picopro_ui_sim.py
```

Use it only if your Python build includes Tkinter. The browser version is the
portable/default path.

The Tkinter simulator accepts `--demo wavetable` or the `3` key for the
Wavetable layered waveform view.
