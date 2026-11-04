# Findings: PurpleAir cloud API as a third fallback

## Status (2026-11-04): fallback implemented

Built as an opt-in fallback after local HTTP (the device has no MQTT path for this sensor yet), with
these choices: switch after 5 min without a good local poll; poll the cloud every 2 min; stale when
the cloud data is 10 min old; AQI from PM2.5 ATM with the **pre-2024** EPA breakpoints, to match the
sensor's own `pm2.5_aqi`; no EPA humidity correction. Settings: `setpurpleair` console command (see
README). Field names below were checked against the API docs (`api.purpleair.com/api_data.json`):
`pm1.0_atm_a/_b`, `pm2.5_atm_a/_b`, `pm10.0_atm_a/_b`, `last_seen`, top-level `time_stamp`, header
`X-API-Key`, and `read_key` for private sensors. Not yet tested on hardware or with a real key.
Roaming (nearby sensors) is still parked.

Planning only, 2026-10-04. No code written, no API calls made. Question: if both MQTT (from HA) and the display's own
HTTP polling fail, should the display fetch the sensor's data from the PurpleAir cloud API?

## Verdict

**Technically feasible, moderate effort. Not decided; part of the open data source design** (MQTT vs local HTTP vs
cloud).

- **As a home fallback it adds little.** The cloud only has data the sensor uploaded; in most failures that stop
  local polling, the sensor has stopped uploading too.
- **Its real use case is a display moved away from home**, with no local sensor or broker to read. Then the cloud is
  the only source, and it can show any public sensor, not just this one.
- **The catch:** the cloud has no AQI or AQI colour fields, so the firmware would have to compute both from PM2.5.
  The AQI category colour itself is a fixed lookup once AQI is known; the work is the AQI calculation and accepting
  that it may not exactly match the sensor's own `pm2.5_aqi` / `p25aqic`.

## When the third fallback would actually run

The chain would be MQTT, then local HTTP, then the cloud. The cloud is only reached when HA/MQTT **and** the local
sensor are both unavailable to the display.

| Failure | MQTT | Local HTTP | Cloud | Cloud helps? |
| --- | --- | --- | --- | --- |
| Normal 36 s stall | quiet | times out | not updated yet (2 min cycle) | No. The stall is shorter than the cloud's update cycle |
| Sensor off, or its Wi-Fi down | stops | fails | stops updating | No. Same last value the display already holds |
| Home internet down | works | works | unreachable | No, not needed |
| Display's Wi-Fi down | fails | fails | fails | No |
| HA down + sensor's IP changed (no DHCP reservation) | fails | fails (wrong IP) | works | **Yes** |
| HA down + sensor's local web server hung, upload still working | fails | fails | works | **Yes**, if that happens at all (never seen) |

## At home: what it would cover, and the cheaper fix

At home, the realistic case is the hard-coded sensor IP going wrong while HA is also down. That is better fixed at
the source:

- a DHCP reservation for the sensor on the router (no code), and/or
- the existing low-priority task "store the sensor address in NVS, set it with a console command".

## What it would take (if built anyway)

### Account and cost

- An API **read key** (`X-API-Key` header) and the sensor's **`sensor_index`** (a number, not the MAC).
- Paid by points: new accounts get 1,000,000 free points; bought points cost about 100,000 per USD at the lowest tier.
  Per call: a base cost of at most 5 points plus a per-field cost (1-2 points per field in PurpleAir's example). For
  ~10 fields, roughly 15-25 points per call.
- **As a fallback only, the cost is close to zero.** Even if it ran nonstop at one call per 2 minutes (the cloud's
  update cycle), that is ~720 calls/day, about 11,000-18,000 points/day: the free points would last 2-3 months.
- PurpleAir says owners can read their own sensor for free (on request to PurpleAir).

### Firmware

- **HTTPS.** The cloud is `https://api.purpleair.com/v1/sensors/<index>`. The build already has TLS (mbedTLS, the
  certificate bundle `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, and PSRAM), so `esp_http_client` with `esp_crt_bundle_attach`
  is enough. A TLS handshake needs tens of KB of heap and a few seconds; fine at one call per 2 minutes.
- **Different response.** The data is nested under `"sensor"`, and the field names differ from the local
  `/json` (for example `pm2.5_atm_a` instead of `pm2_5_atm`, `pm1.0_atm_a`, `pm10.0_atm_a`, `voc`; to be confirmed
  against the API docs). It needs its own field map.
- **No AQI or colour.** The cloud does not return the local `pm2.5_aqi` or `p25aqic` fields. The firmware would have to
  compute AQI and its colour from PM2.5 itself (EPA breakpoints), so the AQI shown in cloud mode could differ from the
  sensor's own number. That's the largest piece of new code.
- **Age without a clock.** The response carries the server's time and the sensor's `last_seen` (both Unix seconds),
  so their difference gives the data's age without SNTP. Cloud data is always at least ~2 minutes old; the display
  should label it (for example "CLOUD") and keep STALE logic based on that age.
- **Storage:** the API key and sensor index in NVS, set by a console command (like `setmqtt`). Not compiled in.
- **One more state** in the source state machine (`MQTT` / `HTTP` / `CLOUD`), entered only after local HTTP has failed
  for a while (well past the 36 s stall, for example 3-5 minutes), and left as soon as local HTTP answers again. Local
  polling has to keep running in cloud mode to notice that.

### Rough size

| Piece | Effort |
| --- | --- |
| HTTPS client + key/index in NVS + console command | Small |
| Cloud field map and parser | Small |
| AQI + colour from PM2.5 | Medium (breakpoint table, tests) |
| Third state, switching rules, "CLOUD" label | Medium, and it multiplies the test cases of the MQTT/HTTP switch |

Order relative to MQTT and HTTP is part of the open design.

## If you want it later

1. Settle the data source design first (MQTT vs HTTP priority, how a display knows it is away from home).
2. Confirm the field names and per-field costs on the developer dashboard (`develop.purpleair.com`), and ask PurpleAir
   for owner (free) access.
3. Build it as a separate, opt-in feature (off when no API key is set).

## Roaming: nearby sensors (lowest priority)

**Idea, liked, parked.** Lowest priority: only after the open firmware tasks and the data source design are settled.
For a display away from home: show the air quality from public PurpleAir sensors near it, not from one fixed sensor.

### 1. Know where the display is

No GPS on the device.

| Option | Accuracy | Cost and effort |
| --- | --- | --- |
| Set by hand (console command or settings screen; lat/lon, or a ZIP code turned into coordinates) | Exact | No service; one step per move |
| IP geolocation (free lookup service) | City level, often several km off; worse on mobile hotspots | One HTTPS call, often no key |
| Wi-Fi geolocation (send the visible access points to a geolocation service) | Tens of metres | API key; the ESP32 can already scan access points |

Likely choice: IP geolocation by default, with a manual override.

### 2. Find sensors nearby

`GET /v1/sensors` with a bounding box (`nwlat`, `nwlng`, `selat`, `selng`), about ±0.05° (~5 km) around the
location, plus `location_type=0` (outdoor only), `max_age` (skip sensors that stopped reporting) and a short `fields=`
list (`latitude`, `longitude`, a PM2.5 value or its 10-minute average, `last_seen`, channel A/B or a confidence value).
The reply is a compact table, one row per sensor. **Parameter and field names are from memory; check them against the
API docs.**

### 3. Pick one sensor or combine several

- Nearest sensor: simplest, but one bad sensor (failing laser, next to a grill) skews everything.
- **Median of the nearest 3-5:** more robust, still simple; ignores one outlier.
- Filter first: drop sensors not seen in ~30 min and those whose A and B channels disagree badly.

### 4. Keep it cheap

- Area search only at boot or when the location changes; then poll just the chosen sensors by `sensor_index`.
- Poll every 2-10 minutes (cloud data updates every 2 minutes).
- Rough cost: an area search with 20 sensors × 5 fields is a few hundred points, once per move. Polling 3 sensors every
  5 minutes is roughly 10-20k points/day, so the free million lasts ~2-3 months; one sensor or every 10 minutes
  stretches it a lot. Cheap if the display only roams now and then.

### What changes on the display

- AQI and its colour computed on the device from PM2.5 (already needed for any cloud data; a median has no single
  sensor AQI anyway).
- Decide whether to apply the US EPA correction (raw PurpleAir PM2.5 tends to read high; the correction needs
  humidity), to match the PurpleAir map, or show raw numbers.
- Show the source on screen, e.g. "Nearby: 3 sensors, ~2 km", so it's clear the reading isn't local.
- Away from home there is no MQTT or local HTTP, so this is the whole data path in that mode: part of the same data
  source state machine.

Sources: [PurpleAir API pricing](https://community.purpleair.com/t/api-pricing/4523); field names and the `last_seen`
/ time stamp fields are from the PurpleAir API documentation as recalled, not checked in this session.
