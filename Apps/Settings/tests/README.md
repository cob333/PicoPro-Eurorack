# Settings host regression tests

Run from the repository root with Clang and the installed Adafruit GFX headers.
These tests use hardware stubs; they are not linked into the firmware and do not
replace board testing. Executables are written outside the repository.

```sh
PICOPRO_TEST_GFX="/Users/yangwenhaomac/Documents/Arduino/libraries/Adafruit_GFX_Library"
clang++ -std=c++11 -O2 -Wall -Wextra -fsanitize=address,undefined -IApps/Settings/tests/stubs Apps/Settings/tests/settings_test.cpp Apps/Settings/SettingsCalibration.cpp -o /tmp/picopro-settings-test
/tmp/picopro-settings-test
clang++ -std=c++11 -O2 -Wall -Wextra -fsanitize=address,undefined -IApps/Settings/tests/stubs -I"$PICOPRO_TEST_GFX" Apps/Settings/tests/ui_test.cpp Apps/Settings/SettingsUI.cpp Apps/Settings/SettingsCalibration.cpp -o /tmp/picopro-settings-ui-test
/tmp/picopro-settings-ui-test
clang++ -std=c++11 -O2 -Wall -Wextra -fsanitize=address,undefined -IApps/Settings/tests/stubs -I"$PICOPRO_TEST_GFX" Apps/Settings/tests/layout_test.cpp -o /tmp/picopro-settings-layout-test
/tmp/picopro-settings-layout-test
clang++ -std=c++11 -O2 -Wall -Wextra -fsanitize=address,undefined Apps/Settings/tests/config_abi_test.cpp -o /tmp/picopro-settings-abi-test
/tmp/picopro-settings-abi-test
clang++ -std=c++11 -O2 -Wall -Wextra -fsanitize=address,undefined -Ilib/PicoProlib Apps/Settings/tests/runtime_test.cpp -o /tmp/picopro-system-runtime-test
/tmp/picopro-system-runtime-test
```

Coverage includes navigation and release guards, all eight calibration completion
bits, fitting/validation and save failures, startup-cache reuse, static-page idle
redraw suppression, live ADC refresh, confirmation defaults, real font bounds,
BootConfig ABI/flag preservation, and stereo/mono arithmetic and rotation modes.
