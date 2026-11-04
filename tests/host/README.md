# Host unit tests

Unit tests for the firmware's hardware-independent logic. They build and run on
a normal Linux/macOS machine with CMake and a C compiler. ESP-IDF, FreeRTOS,
LVGL and the device itself aren't needed.

```bash
python3 scripts/host_tests.py            # configure + build + ctest (ASan/UBSan on)
python3 scripts/host_tests.py --clean    # from scratch
```

or by hand:

```bash
cmake -S tests/host -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
```

[Unity](https://github.com/ThrowTheSwitch/Unity) (v2.6.1) and
[cJSON](https://github.com/DaveGamble/cJSON) (v1.7.18) are fetched at pinned
tags on first configure. To build offline, pass
`-DFETCHCONTENT_SOURCE_DIR_UNITY=<path>` and `-DFETCHCONTENT_SOURCE_DIR_CJSON=<path>`.

## What is covered

| Module (under `main/`) | Responsibility | Device-side caller |
| --- | --- | --- |
| `ha/ha_payload.[ch]` | HA MQTT switch/sensor payload parse + format | `ha/ha_switch.c`, `ha/ha_sensor.c` |
| `sensor/sensor_format.[ch]` | RP2040 legacy sensor packet decode, sensor card text | `sensor/sensor_model.c`, `sensor/sensor_view.c` |
| `rp2040/rp2040_protocol.[ch]` | packet constants, checksum/validation, command framing, COBS stream deframer | `rp2040/rp2040.c` |
| `display/display_logic.[ch]` | backlight duty, default config, sleep timer arithmetic | `display/display_model.c`, `display/display_view.c` |
| `util/indicator_util.c` | broker URL / IPv4 / RSSI helpers | `ha/ha_config.c`, `cmd/cmd.c`, Wi-Fi views |
| `util/cobs.c` | COBS codec | via `rp2040_protocol.c` |

The device-side callers keep the ESP-IDF / MQTT client / FreeRTOS / LVGL work
(publishing, event posting, locking, logging). The pure modules return status
codes, and the callers turn those into the same log lines and actions as before.

## Rules

- Pure modules may include only libc, cJSON and other pure project headers.
  `scripts/architecture_scan.py` (`pure-module-include`) enforces this for the
  files listed in `PURE_MODULE_FILES`.
- `stubs/` only contains a no-op `esp_log.h` for `indicator_util.c`. Don't add
  more stubs to get a module compiling. Split the hardware part out instead.
- Tests labelled *quirk* pin down current wire-protocol behavior, for example
  prefix topic/key matching and only reading the first JSON member. They
  describe what the device does today. If one needs to change, treat it as a
  protocol change: MQTT topics and payloads are product behavior.

## Adding a test

1. Put the pure logic in a `*.c/*.h` pair under the relevant `main/<domain>/`.
   `main/CMakeLists.txt` picks it up automatically.
2. Add the `.c` file to `fw_pure` in `tests/host/CMakeLists.txt`, and add both
   files to `PURE_MODULE_FILES` in `scripts/architecture_scan.py`.
3. Add `tests/host/test_<name>.c` (Unity) and list it in `HOST_TESTS`.
