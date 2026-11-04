#!/usr/bin/env python3
"""Host tests for the PurpleAir cloud fallback: AQI maths and the cloud reply parser.

Both are pure C (main/sensor/aqi.c, main/sensor/network_sensor_cloud_parse.c), so this builds
them with the host C compiler and calls them through ctypes. The parser needs cJSON, taken from
ESP-IDF ($IDF_PATH or ~/.espressif/esp-idf); without it the parser tests are skipped and the
AQI tests still run.
"""

from __future__ import annotations

import ctypes
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SENSOR = ROOT / "main" / "sensor"


def _find_cjson() -> Path | None:
    candidates = []
    if os.environ.get("IDF_PATH"):
        candidates.append(Path(os.environ["IDF_PATH"]))
    candidates.append(Path.home() / ".espressif" / "esp-idf")
    for idf in candidates:
        d = idf / "components" / "json" / "cJSON"
        if (d / "cJSON.c").is_file():
            return d
    return None


def _sensor_types() -> dict[str, int]:
    """Enum values of sensor_data_type, read from the X-macro list in view_data_types.h."""
    text = (ROOT / "main" / "view_data_types.h").read_text()
    names = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("X(") and "," in line:
            names.append(line[2:].split(",")[0].strip())
    return {n: i for i, n in enumerate(names)}


CC = shutil.which("cc") or shutil.which("gcc")
CJSON = _find_cjson()
_tmp = tempfile.TemporaryDirectory()


def _build(name: str, sources: list[Path], includes: list[Path]) -> ctypes.CDLL:
    out = Path(_tmp.name) / f"lib{name}.so"
    cmd = [CC, "-shared", "-fPIC", "-O1", "-Wall", "-Werror", "-o", str(out)]
    cmd += [f"-I{p}" for p in includes] + [str(s) for s in sources] + ["-lm"]
    subprocess.run(cmd, check=True)
    return ctypes.CDLL(str(out))


class Value(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("value", ctypes.c_float)]


class Reading(ctypes.Structure):
    _fields_ = [("values", Value * 10), ("value_count", ctypes.c_int), ("age_s", ctypes.c_int64)]


@unittest.skipIf(CC is None, "no host C compiler")
class AqiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = _build("aqi", [SENSOR / "aqi.c"], [SENSOR])
        cls.lib.aqi_from_pm25.argtypes = [ctypes.c_float]
        cls.lib.aqi_from_pm25.restype = ctypes.c_int
        cls.lib.aqi_color_rgb.argtypes = [ctypes.c_int]
        cls.lib.aqi_color_rgb.restype = ctypes.c_uint32

    def aqi(self, pm: float) -> int:
        return self.lib.aqi_from_pm25(pm)

    def test_breakpoint_edges(self):
        # Pre-2024 EPA table: each row's low and high concentration map to its AQI range ends
        cases = {
            0.0: 0, 12.0: 50, 12.1: 51, 35.4: 100, 35.5: 101, 55.4: 150, 55.5: 151,
            150.4: 200, 150.5: 201, 250.4: 300, 250.5: 301, 350.4: 400, 350.5: 401, 500.4: 500,
        }
        for pm, expected in cases.items():
            with self.subTest(pm=pm):
                self.assertEqual(self.aqi(pm), expected)

    def test_interpolated_values(self):
        # Hand-checked against the EPA formula
        self.assertEqual(self.aqi(8.0), 33)     # 50/12 * 8 = 33.3
        self.assertEqual(self.aqi(20.0), 68)    # 49/23.3 * 7.9 + 51 = 67.6
        self.assertEqual(self.aqi(100.0), 174)  # 49/94.9 * 44.5 + 151 = 174.0

    def test_truncates_to_tenths(self):
        # EPA truncates PM2.5 to one decimal: 12.09 is 12.0 (AQI 50), not in the 12.1 row
        self.assertEqual(self.aqi(12.09), 50)
        self.assertEqual(self.aqi(35.49), 100)

    def test_out_of_range(self):
        self.assertEqual(self.aqi(-3.0), 0)
        self.assertEqual(self.aqi(float("nan")), 0)
        self.assertEqual(self.aqi(600.0), 500)

    def test_category_colors(self):
        cases = {0: 0x00E400, 50: 0x00E400, 51: 0xFFFF00, 100: 0xFFFF00, 101: 0xFF7E00,
                 151: 0xFF0000, 201: 0x8F3F97, 301: 0x7E0023, 500: 0x7E0023}
        for aqi, rgb in cases.items():
            with self.subTest(aqi=aqi):
                self.assertEqual(self.lib.aqi_color_rgb(aqi), rgb)


@unittest.skipIf(CC is None, "no host C compiler")
@unittest.skipIf(CJSON is None, "cJSON not found (set IDF_PATH)")
class CloudParseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = _build(
            "cloudparse",
            [SENSOR / "aqi.c", SENSOR / "network_sensor_cloud_parse.c", CJSON / "cJSON.c"],
            [SENSOR, ROOT / "main", CJSON],
        )
        cls.lib.network_sensor_cloud_parse.argtypes = [
            ctypes.c_char_p, ctypes.POINTER(Reading), ctypes.c_char_p, ctypes.c_int]
        cls.lib.network_sensor_cloud_parse.restype = ctypes.c_int
        cls.types = _sensor_types()

    def parse(self, payload) -> tuple[int, Reading, str]:
        text = payload if isinstance(payload, str) else json.dumps(payload)
        reading = Reading()
        err = ctypes.create_string_buffer(48)
        ret = self.lib.network_sensor_cloud_parse(text.encode(), ctypes.byref(reading), err, len(err))
        return ret, reading, err.value.decode()

    def values(self, reading: Reading) -> dict[str, float]:
        by_id = {v: k for k, v in self.types.items()}
        return {by_id[reading.values[i].type]: reading.values[i].value for i in range(reading.value_count)}

    @staticmethod
    def rgb(value: float) -> int:
        return struct.unpack("<I", struct.pack("<f", value))[0]

    # Shape from the PurpleAir API docs for GET /v1/sensors/:sensor_index
    REPLY = {
        "api_version": "V1.2.3-1.1.45",
        "time_stamp": 1793771127,
        "data_time_stamp": 1793771100,
        "sensor": {
            "sensor_index": 123456,
            "last_seen": 1793771007,
            "pm1.0_atm_a": 1.5, "pm1.0_atm_b": 1.7,
            "pm2.5_atm_a": 20.0, "pm2.5_atm_b": 8.0,
            "pm10.0_atm_a": 25.2, "pm10.0_atm_b": 9.9,
        },
    }

    def test_full_reply(self):
        ret, reading, _ = self.parse(self.REPLY)
        self.assertEqual(ret, 0)
        self.assertEqual(reading.age_s, 120)
        v = self.values(reading)
        self.assertAlmostEqual(v["NETWORK_SENSOR_PM1_0_A"], 1.5, places=4)
        self.assertAlmostEqual(v["NETWORK_SENSOR_PM2_5_B"], 8.0, places=4)
        self.assertAlmostEqual(v["NETWORK_SENSOR_PM10_0_A"], 25.2, places=4)
        self.assertEqual(v["NETWORK_SENSOR_PM25_AQI_A"], 68)
        self.assertEqual(v["NETWORK_SENSOR_PM25_AQI_B"], 33)
        self.assertEqual(self.rgb(v["NETWORK_SENSOR_AQI_COLOR_A"]), 0xFFFF00)
        self.assertEqual(self.rgb(v["NETWORK_SENSOR_AQI_COLOR_B"]), 0x00E400)
        self.assertNotIn("NETWORK_SENSOR_GAS_A", v)  # the cloud has no gas reading

    def test_single_channel(self):
        reply = json.loads(json.dumps(self.REPLY))
        for k in ("pm1.0_atm_b", "pm2.5_atm_b", "pm10.0_atm_b"):
            del reply["sensor"][k]
        ret, reading, _ = self.parse(reply)
        self.assertEqual(ret, 0)
        v = self.values(reading)
        self.assertIn("NETWORK_SENSOR_PM25_AQI_A", v)
        self.assertNotIn("NETWORK_SENSOR_PM25_AQI_B", v)

    def test_api_error(self):
        # Real reply from api.purpleair.com without a key, 2026-11-04
        ret, _, err = self.parse({
            "api_version": "V1.2.3-1.1.45", "time_stamp": 1793771127,
            "error": "ApiKeyMissingError", "description": "No API key was found in the request.",
        })
        self.assertEqual(ret, -2)
        self.assertEqual(err, "ApiKeyMissingError")

    def test_no_pm_or_no_timestamps(self):
        reply = {"time_stamp": 1, "sensor": {"last_seen": 1}}
        self.assertEqual(self.parse(reply)[0], -3)
        reply = json.loads(json.dumps(self.REPLY))
        del reply["sensor"]["last_seen"]
        self.assertEqual(self.parse(reply)[0], -3)

    def test_bad_json(self):
        self.assertEqual(self.parse("not json")[0], -1)
        self.assertEqual(self.parse({"time_stamp": 1})[0], -1)

    def test_clock_skew_never_negative(self):
        reply = json.loads(json.dumps(self.REPLY))
        reply["sensor"]["last_seen"] = reply["time_stamp"] + 30
        ret, reading, _ = self.parse(reply)
        self.assertEqual(ret, 0)
        self.assertEqual(reading.age_s, 0)

    def test_requested_fields_match_parser(self):
        # Every field the firmware pays for is one the parser reads, and vice versa
        header = (SENSOR / "network_sensor_cloud_parse.h").read_text()
        fields = header.split('#define NETWORK_SENSOR_CLOUD_FIELDS')[1].split('"')[1].split(",")
        source = (SENSOR / "network_sensor_cloud_parse.c").read_text()
        parsed = {f for f in fields if f'"{f}"' in source}
        self.assertEqual(set(fields), parsed)


if __name__ == "__main__":
    unittest.main()
