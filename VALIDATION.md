# Validation

## English/Czech update — November 3, 2026

- Six Chromium tests passed against the actual embedded setup page: browser-language selection and persistence, complete translation keys, draft preservation without submission, language changes during an in-flight save, localized validation/storage/network errors, blocked browser storage and a 360-pixel viewport.
- MSVC 2022 / C++17: `tests/language_test.cpp` passed, including late translation of an existing status, unchanged diagnostic codes and battery format arguments.
- MSVC 2022 / C++17: the existing `tests/logic_test.cpp` passed, covering voice/touch cancellation, hidden controls, solar events, schedule boundaries, BLE hysteresis/timeouts, battery safeguards and identity validation.
- ESPHome 2026.10.0: full ESP32-S3 firmware build passed, including Czech font glyphs. Reported RAM: 170,771 / 341,760 bytes; flash: 2,325,831 / 8,126,464 bytes. Existing floating-point-to-display-coordinate warnings remain.
- Existing entity IDs, names, selector option values and the saved network/BLE structure were compared with the previous source and preserved.
- Gitleaks 8.30.1 passed the staged-source scan; the outgoing commit is scanned again before publication. Generated configuration, credentials, build output and browser artifacts are excluded.

Browser requests used synthetic fixtures. No real credentials were submitted, no live network configuration was changed and no device was flashed. Home Assistant installation, real-device language restoration, physical screen rendering and the existing robot-eye rendering suite were not rerun for this update. Language persistence uses ESPHome's existing template-selector restoration mechanism.

## Original source snapshot — November 1, 2026

- MSVC C++17: `tests/logic_test.cpp` passed its logic, touch, time, BLE and battery checks.
- Gitleaks 8.30.1 passed the exported-source scan. Examples contained placeholders only.
- Tests ran on Windows with MSVC 2022 and locally available libraries.

The complete firmware build, board installation and tests connected to a physical device were not performed during that import. The source snapshot was not a new binary release.
