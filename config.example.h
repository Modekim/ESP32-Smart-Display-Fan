#pragma once
/**
 * @file config.example.h
 * @brief Template configuration. Copy to `config.h` (NOT committed) and fill in your secrets.
 */

// --- Wi-Fi ---
#define CFG_WIFI_SSID       "YOUR_WIFI_SSID"
#define CFG_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// --- MQTT ---
#define CFG_MQTT_HOST       "192.168.1.100"
#define CFG_MQTT_PORT       1883
#define CFG_MQTT_USER       "mqtt_user"
#define CFG_MQTT_PASSWORD   "mqtt_password"

// (optional) hostname
#define CFG_WIFI_HOSTNAME   "esp32-fan-node"

// --- Fan hysteresis defaults ---
#define CFG_FAN_T_ON_C_DEFAULT   30.0f
#define CFG_FAN_T_OFF_C_DEFAULT  25.0f

// --- DS18B20 publish policy (delta + min interval) ---
#define CFG_DS18B20_DELTA_MIN_DEFAULT   0.25f   // °C
#define CFG_DS18B20_PUB_MIN_MS_DEFAULT  60000UL // ms

// --- Timezone (Europe/Warsaw) ---
#define CFG_TZ_STD_OFFSET   3600   // CET = UTC+1 (winter)
#define CFG_TZ_DST_OFFSET   7200   // CEST = UTC+2 (summer)
