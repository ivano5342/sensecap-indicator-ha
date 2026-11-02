# Sensor Domain

The Sensor domain is a vertical slice for built-in sensor data received from the
RP2040 side of the SenseCAP Indicator.

## Files

| File | Responsibility |
| --- | --- |
| `sensor.h` | Umbrella header for the Sensor domain. Its include guard is `SENSOR_H` for compatibility with existing feature checks. |
| `sensor_model.h` | Sensor types, status/error definitions, RP2040 parser API, and current-value cache API. |
| `sensor_model.c` | Parses RP2040 sensor packets, maintains the latest `SensorData` cache, and posts `VIEW_EVENT_SENSOR_DATA`. |
| `sensor_view.h` | Sensor view initialization API. |
| `sensor_view.c` | Subscribes to `VIEW_EVENT_SENSOR_DATA` and updates LVGL labels. Skips types that have no card on its tile. |
| `sen66_view.h` | SEN66 view initialization API. |
| `sen66_view.c` | Builds the SEN66 tile, a 3x3 grid of its nine values, and updates it from `VIEW_EVENT_SENSOR_DATA`. |

## Init Sequence

`main/main.c` calls `indicator_view_init()` before `indicator_model_init()`.
The Sensor view subscribes to display events during view initialization, and the
Sensor model initializes the cache and mutex during model initialization.

## Data Flow

RP2040 UART data is dispatched to `_sensor_data_parse_handle()`.
The parser maps packet types to sensor IDs and calls `update_sensor_data()`.
The model updates the cached value and posts `VIEW_EVENT_SENSOR_DATA`.
`sensor_view.c` handles that event and updates the matching LVGL labels while
holding the LVGL port semaphore.

## SEN66 Tile

SEN66 packets `0xC1`-`0xC7` map to the `SEN66_SENSOR_*` types. Its temperature and
humidity arrive on the shared `0xB3`/`0xB4` packets as `SHT41_SENSOR_*`.

Each card shows `N/A` until its first reading. If no `SEN66_SENSOR_*` packet arrives
for 30 seconds, an `lv_timer` dims the cards and shows `STALE`; the next one clears it.
Shared packets don't count as SEN66 activity, and the tile shows their values only
while SEN66 data is live, because they can also come from the SHT41 or AHT20.

## UI Containment

Only `sensor_view.c` and `sen66_view.c` may own LVGL label references for sensor
values. Model files must remain UI-free.
