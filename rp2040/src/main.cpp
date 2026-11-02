/**
 * @file rp2040.ino
 * @date  01 March 2024

 * @author Spencer Yan
 *
 * @note :
 * Product Wiki:
https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/
 * System Diagram:
https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_6.png
 * Grove Ports:
https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/grove.png
 *
 * @copyright © 2024, Seeed Studio
 */

#include "indicator_rp2040.hpp"

PacketSerial packetSerial;

static bool has_aht   = false;
static bool has_sht41 = false;

/************************ power requests  ****************************/

// onPacketReceived() runs on core 1 and loop() on core 0, which owns Wire and the
// sensor power switch. Core 1 only records the latest request; the sequence number
// has one writer per side, so no lock is needed.
enum power_request_type : uint8_t {
    POWER_REQUEST_ON,
    POWER_REQUEST_OFF,
};

static volatile power_request_type power_request     = POWER_REQUEST_ON;
static volatile uint32_t           power_request_seq = 0;

static void power_request_set(power_request_type request)
{
    power_request = request;
    power_request_seq = power_request_seq + 1;
}

/************************ recv cmd from esp32  ****************************/

/**
 * @brief the function when received data(pre-defined) from ESP32 
 * @param buffer
 * @param size
*/
void onPacketReceived(const uint8_t* buffer, size_t size)
{
#if DEBUG
    Serial.printf("<--- recv len:%d, data: ", size);
    for (int i = 0; i < size; i++) {
        Serial.printf("0x%x ", buffer[i]);
    }
    Serial.println("");
#endif
    if (size < 1) {
        return;
    }
    switch (buffer[0]) {
        case PKT_TYPE_CMD_SHUTDOWN:
            power_request_set(POWER_REQUEST_OFF);
            break;
        case PKT_TYPE_CMD_POWER_ON:
            power_request_set(POWER_REQUEST_ON);
            break;
        case PKT_TYPE_CMD_BEEP_ON:
            beep_on();
            break;
        case PKT_TYPE_CMD_BEEP_OFF:
            beep_off();
            break;
        case PKT_TYPE_CMD_RESCAN_GROVE:
            Serial.println("cmd rescan grove: no dynamic Grove scanner in this firmware yet");
            break;
        default:
            break;
    }
}

/************************ setuo & loop ****************************/

// static bool sd_init_flag = 0;

static void sensors_init(void)
{
    has_aht   = sensor_aht_init();
    has_sht41 = sensor_sht41_init();
    sensor_sgp40_init();
    sensor_scd4x_init();
    sensor_sen66_init();
}

void setup()
{
    Serial.begin(115200);  // Connect to PC via USB

    // The Serial connect to inner ESP32S3
    Serial1.setRX(17);  //
    Serial1.setTX(16);
    Serial1.begin(115200);
    // Prepare PacketSerial, which will reduce the sent data size
    packetSerial.setStream(&Serial1);
    packetSerial.setPacketHandler(&onPacketReceived);

    // The built-in sensor needs to be powered on
    sensor_power_on();

    // I2C init
    Wire.setSDA(20);
    Wire.setSCL(21);
    Wire.begin();

    beep_init();
    // beep_on();

    sensors_init();

    if (has_aht) {
        sensor_attached_send(packetSerial, PKT_SENSOR_ID_AHT20_TEMP, PKT_SENSOR_CAT_TEMP, "AHT20", "C");
        sensor_attached_send(packetSerial, PKT_SENSOR_ID_AHT20_HUMIDITY, PKT_SENSOR_CAT_HUMIDITY, "AHT20", "%RH");
    }
    if (has_sht41) {
        sensor_attached_send(packetSerial, PKT_SENSOR_ID_GROVE_BASE, PKT_SENSOR_CAT_TEMP, "SHT41", "C");
        sensor_attached_send(packetSerial, PKT_SENSOR_ID_GROVE_BASE + 1, PKT_SENSOR_CAT_HUMIDITY, "SHT41", "%RH");
    }
    sensor_attached_send(packetSerial, PKT_SENSOR_ID_SCD41_CO2, PKT_SENSOR_CAT_CO2, "SCD41", "ppm");
    sensor_attached_send(packetSerial, PKT_SENSOR_ID_SGP40_VOC, PKT_SENSOR_CAT_VOC, "SGP40", "idx");
}

/*************************** delay *****************************/
// NonBlockingDelay adc_delay(3800);
NonBlockingDelay aht_delay(2000);  //
NonBlockingDelay sgp40_delay(3800);  // Tvoc
NonBlockingDelay scd4x_delay(3800);  // CO2 Above 3500 would be good
NonBlockingDelay sen66_delay(5000);

/*************************** data to send *****************************/
AHTData   data_aht = {0};
SPG40Data data_spg = {0};
SCD4XData data_scd = {0};
SEN66Data data_sen66 = {0};

/************************ compensation  ****************************/
uint16_t defaultCompenstaionRh = 0x8000;
uint16_t defaultCompenstaionT  = 0x6666;

#define USING_AHT_COMPENSATION 1
uint16_t SPG4x_compensationRh = defaultCompenstaionRh;
uint16_t SPG4x_compensationT  = defaultCompenstaionT;

/**
 * @brief Calibrate the sensor data
 *
 * @param rawValue the raw value
 * @param scaleFactor the scale factor default is 1.0
 * @param offset  the offset default is 0.0
 * @return float
 */
float calibrateSensor(float rawValue, float scaleFactor = 1.0, float offset = 0.0)
{
    float calibratedValue = (rawValue * scaleFactor) + offset;
    return calibratedValue;
}

/************************ END ****************************/

#define SEN66_CLIMATE_TIMEOUT_MS 15000

static bool     sensors_powered       = true;
static bool     sen66_climate_seen    = false;
static uint32_t sen66_climate_seen_ms = 0;

static void power_requests_handle(void)
{
    static uint32_t handled_seq = 0;

    uint32_t seq = power_request_seq;
    if (seq == handled_seq) {
        return;
    }
    handled_seq = seq;

    if (power_request == POWER_REQUEST_OFF) {
        Serial.println("cmd shutdown");
        sensor_power_off();
        sensors_powered = false;
        return;
    }

    Serial.println("cmd power on");
    if (!sensors_powered) {
        sensor_power_on();
        delay(100);  // let the sensors start before talking to them
        sensors_powered = true;
    }
    sensors_init();
}

static void sgp40_compensation_update(float humidity, float temperature)
{
#if USING_AHT_COMPENSATION
    humidity    = constrain(humidity, 0.0f, 100.0f);
    temperature = constrain(temperature, -45.0f, 130.0f);

    SPG4x_compensationRh = static_cast<uint16_t>(humidity * 65535 / 100);
    SPG4x_compensationT  = static_cast<uint16_t>((temperature + 45) * 65535 / 175);
#endif
}

static void temperature_humidity_update(void)
{
    if (!has_aht) {
        has_aht = sensor_aht_init();
        if (has_aht) {
            sensor_attached_send(packetSerial, PKT_SENSOR_ID_AHT20_TEMP, PKT_SENSOR_CAT_TEMP, "AHT20", "C");
            sensor_attached_send(packetSerial, PKT_SENSOR_ID_AHT20_HUMIDITY, PKT_SENSOR_CAT_HUMIDITY, "AHT20", "%RH");
        }
    }
    if (!has_sht41) {
        has_sht41 = sensor_sht41_init();
        if (has_sht41) {
            sensor_attached_send(packetSerial, PKT_SENSOR_ID_GROVE_BASE, PKT_SENSOR_CAT_TEMP, "SHT41", "C");
            sensor_attached_send(packetSerial, PKT_SENSOR_ID_GROVE_BASE + 1, PKT_SENSOR_CAT_HUMIDITY, "SHT41", "%RH");
        }
    }
    bool used_sht41 = has_sht41 && sensor_sht41_get(data_aht);
    if (!used_sht41 && !(has_aht && sensor_aht_get(data_aht))) {
        SPG4x_compensationRh = defaultCompenstaionRh;
        SPG4x_compensationT  = defaultCompenstaionT;
        return;
    }
    if (used_sht41) {
        Serial.printf("SHT41: humidity: %.2f %% \t temperature: %.2f\n", data_aht.humidity, data_aht.temperature);
    } else {
        Serial.print("AHT20 fallback - ");
        sensor_aht_print(data_aht);
    }

    // get the *calibration* data
    float humidity = calibrateSensor(static_cast<float>(data_aht.humidity), 1, 0.0);  // humidity = 1 * raw_humidity + 0
    sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_HUMIDITY, humidity);  // send the humidity to ESP32
    sensor_value_send(packetSerial, has_sht41 ? PKT_SENSOR_ID_GROVE_BASE + 1 : PKT_SENSOR_ID_AHT20_HUMIDITY, humidity);

    // get the temperature from ATH
    float temperature = calibrateSensor(static_cast<float>(data_aht.temperature));
    sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_TEMP, temperature);
    sensor_value_send(packetSerial, has_sht41 ? PKT_SENSOR_ID_GROVE_BASE : PKT_SENSOR_ID_AHT20_TEMP, temperature);

    sgp40_compensation_update(data_aht.humidity, data_aht.temperature);
}

// Rejects NAN as well, because every comparison with NAN is false.
static bool sen66_value_in_range(float value, float min, float max)
{
    return value >= min && value <= max;
}

static void sen66_value_send(pkt_type type, float value, float min, float max)
{
    if (sen66_value_in_range(value, min, max)) {
        sensor_data_send(packetSerial, type, value);
    }
}

static void sen66_data_send(const SEN66Data& data)
{
    // The sensor encodes PM up to 6553.4 and it is not clipped lower, so that very
    // high readings such as smoke still reach the ESP32.
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_PM1_0, data.pm1p0, 0.0f, 6553.4f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_PM2_5, data.pm2p5, 0.0f, 6553.4f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_PM4_0, data.pm4p0, 0.0f, 6553.4f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_PM10_0, data.pm10p0, 0.0f, 6553.4f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_VOC_INDEX, data.vocIndex, 1.0f, 500.0f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_NOX_INDEX, data.noxIndex, 1.0f, 500.0f);
    sen66_value_send(PKT_TYPE_SENSOR_SEN66_CO2, data.co2, 1.0f, 40000.0f);

    if (sen66_value_in_range(data.humidity, 0.0f, 100.0f) && sen66_value_in_range(data.temperature, -40.0f, 125.0f)) {
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_HUMIDITY, data.humidity);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_TEMP, data.temperature);
        sgp40_compensation_update(data.humidity, data.temperature);
        sen66_climate_seen    = true;
        sen66_climate_seen_ms = millis();
    }
}

// True while the SEN66 is supplying temperature and humidity, which takes priority
// over the SHT41 and AHT20. Those are then neither probed nor read.
static bool sen66_climate_active(void)
{
    return sensor_sen66_present() && sen66_climate_seen && millis() - sen66_climate_seen_ms < SEN66_CLIMATE_TIMEOUT_MS;
}

void loop()
{
    power_requests_handle();
    if (!sensors_powered) {
        return;
    }

    if (scd4x_delay.check() && sensor_scd4x_get(data_scd)) {  // External Sensor, only use CO2; As
        // Temp and Humi is not accurate.
        sensor_scd4x_print(data_scd);

        // don't need to calibrate the co2
        float co2 = calibrateSensor(static_cast<float>(data_scd.co2));
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SCD41_CO2,
                         co2);  // uint16_t to float
        sensor_value_send(packetSerial, PKT_SENSOR_ID_SCD41_CO2, co2);
    }

// Temporarily use SPG40 to get the compensation data
#if USING_AHT_COMPENSATION
    if (sgp40_delay.check() && sensor_sgp40_get(data_spg, SPG4x_compensationRh, SPG4x_compensationT))
#else
    SPG4x_compensationRh = defaultCompenstaionRh;  // modify as you need
    SPG4x_compensationT  = defaultCompenstaionT;  // modify as you need
    if (sgp40_delay.check() && sensor_sgp40_get(data_spg, SPG4x_compensationRh, SPG4x_compensationT))
#endif
    {
        sensor_sgp40_print(data_spg);
        // get the vocIndex from SPG40
        float voxIndex = calibrateSensor(static_cast<float>(data_spg.vocIndex));
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SGP40_TVOC_INDEX, voxIndex);
        sensor_value_send(packetSerial, PKT_SENSOR_ID_SGP40_VOC, voxIndex);
    }

    if (aht_delay.check() && !sen66_climate_active()) {
        temperature_humidity_update();
    }

    if (sen66_delay.check()) {
        if (!sensor_sen66_present()) {
            sensor_sen66_init();
        } else if (sensor_sen66_get(data_sen66)) {
            sensor_sen66_print(data_sen66);
            sen66_data_send(data_sen66);
        }
    }
}

void loop1()
{
    packetSerial.update();
    if (packetSerial.overflow()) {}
}
