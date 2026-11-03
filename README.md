# ESP Desk Buddy

A desktop companion for the Waveshare ESP32-S3-Touch-AMOLED-1.75/1.75-B: animated eyes, touch controls, a clock, BLE presence and Home Assistant Assist.

Version **0.2.8** adds English/Czech display and network-setup interfaces. The hardware profile does **not** support the 1.75C variant.

## Build and configure

Use ESPHome **2026.10.0**, pinned in `requirements.txt`:

```sh
python -m pip install -r requirements.txt
```

Copy `device.example.yaml` to `device.yaml` and `secrets.example.yaml` to `secrets.yaml`. Supply your own network settings, passwords and a randomly generated Home Assistant API encryption key. Keep both local configuration files private; they are ignored by Git.

```sh
esphome config device.yaml
esphome compile device.yaml
```

Voice interaction requires your own Home Assistant Assist pipeline. MQTT does not replace the voice API. Building or uploading these sources does not install firmware on a device.

## English and Czech

- Set **Display language / Jazyk displeje** to **EN** or **CZ** through Home Assistant or the ESPHome device controls. The native ESPHome selector restores its saved value after restart; the normal preference write interval is 30 seconds. The initial language is English.
- The choice changes the clock, voice and power screens, diagnostic text and day-phase descriptions. Switching changes labels in place, preserving the selected screen, volume, microphone state, animations and active conversation. Czech display fonts include accented characters.
- Open `/setup` on the device to change Wi-Fi or MQTT settings. Its **EN/CZ** selector follows the browser language initially and remembers an explicit preference in that browser. This preference is independent of the device display language.
- Switching the setup form language preserves all typed values. It does not submit, save or restart the device. A pending save remains disabled until its response arrives, and messages follow the currently selected language.

Existing Home Assistant entity names and selector option values remain configuration data. They are preserved, together with IDs and user-defined names, so existing installations and automations retain their identifiers. The ESPHome and Home Assistant frontends are supplied by those projects; this repository localizes its own display, status text and setup form. Status **text** follows the selected display language, so use the corresponding wording in any automation that compares textual status values.

The setup form never displays stored passwords. Leave an entire section's SSID or MQTT host empty to preserve that section. For a section you change, enter its password again; an empty password explicitly means no password. **Save and restart** applies the change and restarts the device.

## Checks

Browser tests extract and exercise the actual embedded setup page using synthetic requests:

```sh
npm ci
npx playwright install chromium
npm run test:web
```

Build and run `tests/language_test.cpp` and `tests/logic_test.cpp` with a C++17 compiler. For example, in a Visual Studio developer shell:

```bat
cl /EHsc /std:c++17 /utf-8 tests\language_test.cpp /Fe:language_test.exe
language_test.exe
cl /EHsc /std:c++17 /utf-8 tests\logic_test.cpp /Fe:logic_test.exe
logic_test.exe
```

The language check covers status translation, unchanged diagnostic codes and numeric formatting. The existing logic suite covers touch/voice cancellation, solar schedules, BLE presence and battery safeguards. See [VALIDATION.md](VALIDATION.md) for the checks actually run and their limits.

## Repository contents

Source, build configuration, licensed assets and tests belong here. Credentials, installation records, device backups and generated binaries do not. Fill in example configuration locally.

[Project directory](https://github.com/Jirkas-prog) · [Home Assistant apps](https://github.com/Jirkas-prog/home-assistant-addons)
