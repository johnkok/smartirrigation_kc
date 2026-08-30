#pragma once
#include <Arduino.h>

// ---------- RS-485 ----------
static constexpr int RS485_RX_PIN = 16;
static constexpr int RS485_TX_PIN = 17;
static constexpr int RS485_DE_RE_PIN = 4;
static constexpr uint32_t RS485_BAUD = 4800;

static constexpr uint8_t SENSOR_COUNT = 2;
static constexpr uint8_t ZONE_COUNT = 4;

// Two physical RS-485 sensors; zones 3/4 can be scheduled/manual or mapped
// to either sensor until extra sensors are added.
static constexpr uint8_t SENSOR_IDS[SENSOR_COUNT] = {1, 2};

// Zone -> sensor mapping. -1 means no soil sensor.
static constexpr int8_t ZONE_SENSOR_MAP[ZONE_COUNT] = {0, 1, -1, -1};

static constexpr uint16_t REG_MOISTURE = 0x0000;
static constexpr uint16_t REG_TEMPERATURE = 0x0001;
static constexpr uint16_t REG_EC = 0x0002;
static constexpr uint16_t REG_PH = 0x0003;
static constexpr uint16_t SENSOR_REGISTER_COUNT = 4;

// ---------- Valves ----------
static constexpr int VALVE_PINS[ZONE_COUNT] = {26, 27, 25, 33};
static constexpr bool VALVE_ACTIVE_HIGH = true;

// Optional master valve/pump relay. Set to -1 to disable.
static constexpr int MASTER_VALVE_PIN = 32;
static constexpr bool MASTER_ACTIVE_HIGH = true;
static constexpr uint32_t MASTER_PREOPEN_MS = 1000;
static constexpr uint32_t MASTER_POSTCLOSE_MS = 1500;

// ---------- Inputs ----------
static constexpr int RAIN_PIN = 34;   // input-only on ESP32
static constexpr bool RAIN_ACTIVE_LOW = true;

// Flow meter pulse input. Example common hall sensors provide pulses/L.
// Calibrate for your own meter.
static constexpr int FLOW_PIN = 35;   // input-only
static constexpr float FLOW_PULSES_PER_LITER = 450.0f;

// Safety thresholds
static constexpr uint32_t FLOW_START_GRACE_MS = 8000;
static constexpr float MIN_FLOW_LPM_WHEN_WATERING = 0.10f;
static constexpr float MAX_FLOW_LPM = 30.0f;
static constexpr uint32_t NO_FLOW_TRIP_MS = 15000;

// ---------- Timing ----------
static constexpr uint32_t SENSOR_POLL_MS = 5000;
static constexpr uint32_t SENSOR_STALE_MS = 30000;
static constexpr uint32_t CONTROL_TICK_MS = 1000;
static constexpr uint32_t MQTT_PUBLISH_MS = 5000;

static constexpr const char* TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";
static constexpr const char* NTP_1 = "pool.ntp.org";
static constexpr const char* NTP_2 = "time.google.com";

static constexpr bool SCHEDULE_REQUIRES_DRY_SOIL = false;
static constexpr bool RAIN_BLOCKS_AUTOMATION = true;

static constexpr uint8_t MAX_SCHEDULES_PER_ZONE = 4;

// MQTT/Home Assistant
static constexpr const char* MQTT_BASE = "soil_irrigation";
static constexpr const char* MQTT_DISCOVERY_PREFIX = "homeassistant";
static constexpr const char* FW_VERSION = "2.0.0";

// Weather integration is intentionally driven by Home Assistant:
// HA publishes a weather block/unblock flag via MQTT.
// This avoids putting external API keys in the ESP32.
static constexpr bool WEATHER_BLOCK_DEFAULT = false;
