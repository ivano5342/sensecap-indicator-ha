# RP2040 Firmware

This directory is a PlatformIO project for the SenseCAP Indicator RP2040 coprocessor.
The RP2040 firmware currently uses PlatformIO for compile, upload, dependency,
and serial monitor management.

## Build

Install PlatformIO, then build from this directory:

```sh
cd rp2040
pio run
```

## Upload

Connect the Indicator RP2040 USB port and run:

```sh
cd rp2040
pio run -t upload
```

The project targets `seeed_indicator_rp2040` with the Earle Philhower Arduino
RP2040 core.

## Serial Monitor

```sh
cd rp2040
pio device monitor
```

The monitor speed is `115200`.

## Dependencies

PlatformIO installs the RP2040 sensor dependencies declared in `platformio.ini`:

- `PacketSerial`
- `AHT20`
- `Sensirion I2C SCD4x`
- `Sensirion I2C SGP40`
- `Sensirion I2C SEN66`
- `Sensirion Gas Index Algorithm`

## SEN66

A SEN66 on the Grove I2C port (GPIO20 SDA, GPIO21 SCL, address `0x6B`) is optional.
The firmware retries detection every 5 seconds and marks the sensor absent after 5
read failures in a row.

- **Reading:** every 5 seconds, using the sensor's continuous measurement mode.
- **No device reset:** init never resets the sensor, because the ESP32 sends
  `PKT_TYPE_CMD_POWER_ON` at every boot and a reset could disturb VOC/NOx learning.
- **Packets:** `0xC1`-`0xC7` carry PM1.0, PM2.5, PM4.0, PM10, VOC, NOx and CO2 as type
  + float. A value is sent only if it is known and in range, so CO2, VOC and NOx may
  be missing just after start.
- **Temperature and humidity:** sent on the existing `0xB3`/`0xB4` packets and used
  for SGP40 compensation. The SHT41 and AHT20 are probed and read only if the SEN66
  has given no valid reading for 15 seconds.
- **Power:** check the supply voltage at the sensor with its fan running. The Grove
  port supplies 3.3 V, and one listing gives the SEN66 a 3.15-3.45 V range.
