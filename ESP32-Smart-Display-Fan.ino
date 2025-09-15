/*
  ESP32 OLED + MQTT + DS18B20 + FAN (hysteresis) + OLED UI + EU DST + Power tweaks
  --------------------------------------------------------------------------------
  - OLED (SSD1306 128x64), UI: 3 rows × 2 columns
  - MQTT integration with Home Assistant (HA)
  - DS18B20 publishes using ΔT / min_ms policy (less network traffic, lower power)
  - Fan controlled by MOSFET (low-side), hysteresis, modes: AUTO / ON / OFF
  - NTP + automatic CET/CEST (Europe/Warsaw)
  - Energy optimizations: Wi-Fi power save, TX power tuning, long redraw intervals
  - LWT: online/offline
  - Sensitive data stored in config.h (NOT committed!)

   UI:
    Row 1:  <MQTT Temp °C>               <MQTT Humidity %>
    Row 2:  <MQTT Pressure hPa>          FAN <ON|OFF>
    Row 3:  <DS18B20 Temp (int) °C>      <Last MQTT update HH:MM>
*/

#if !defined(ARDUINO_ARCH_ESP32)
#error "This sketch targets ESP32 (Arduino core). Please select an ESP32 board."
#endif

// --- Secrets/Local Configuration
#include "config.h"

// Std/SDK
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

// ============================================================================
// FIXED CONFIGURATION
// ============================================================================

// --- OLED display & layout ---
constexpr uint8_t  SCREEN_WIDTH  = 128;
constexpr uint8_t  SCREEN_HEIGHT = 64;
constexpr uint8_t  OLED_ADDR     = 0x3C;

// --- Column X positions (both columns are left-aligned) ---
constexpr int16_t COL1_X = 0;
constexpr int16_t COL2_X = 80;

// --- Row Y positions ---
constexpr int16_t ROW_Y0 = 2;
constexpr int16_t ROW_Y1 = 22;
constexpr int16_t ROW_Y2 = 42;

// --- Time & redraw & sampling ---
constexpr uint32_t NTP_POLL_INTERVAL  = 1800000UL;   // 30 min
constexpr uint32_t REDRAW_INTERVAL_MS = 1800000UL;  // safety redraw each 30 min
constexpr uint32_t DS18B20_SAMPLE_MS = 60000UL;    // read every 60 seconds

// --- Pins (ESP32 Dev Module) ---
constexpr uint8_t PIN_I2C_SDA = 21;   // OLED SDA
constexpr uint8_t PIN_I2C_SCL = 22;   // OLED SCL
constexpr uint8_t PIN_ONEWIRE = 4;    // DS18B20 DATA (with 4.7k pull-up to 3V3)
constexpr uint8_t PIN_FAN     = 23;   // MOSFET gate (LOW=OFF, HIGH=ON)

// MQTT topics
// --- MQTT topics (inbound from HA -> values shown on OLED) ---
constexpr const char* TOPIC_TEMP_IN  = "esp32/weather/temperature";
constexpr const char* TOPIC_HUMI_IN  = "esp32/weather/humidity";
constexpr const char* TOPIC_PRESS_IN = "esp32/weather/pressure";

// --- MQTT topics (outbound from ESP32 -> HA, retained) ---
constexpr const char* TOPIC_DS18_OUT       = "esp32/fan/temperature";
constexpr const char* TOPIC_FAN_OUT        = "esp32/fan/state";
constexpr const char* TOPIC_FAN_CMD        = "esp32/fan/cmd";              // "AUTO"|"ON"|"OFF"
constexpr const char* TOPIC_FAN_MODE_STATE = "esp32/fan/mode";             // retained: "AUTO"/"ON"/"OFF"

// --- MQTT retained config state ---
constexpr const char* TOPIC_ON_C_STATE     = "esp32/fan/config/on_c";      // retained float
constexpr const char* TOPIC_ON_C_SET       = "esp32/fan/config/on_c/set";  // command float
constexpr const char* TOPIC_OFF_C_STATE    = "esp32/fan/config/off_c";     // retained float
constexpr const char* TOPIC_OFF_C_SET      = "esp32/fan/config/off_c/set"; // command float

// --- MQTT config: DS18B20 publish settings (delta + min interval) ---
constexpr const char* TOPIC_DELTA_C_STATE = "esp32/fan/config/delta_c";      // retained float
constexpr const char* TOPIC_DELTA_C_SET   = "esp32/fan/config/delta_c/set";  // command float
constexpr const char* TOPIC_MINMS_STATE   = "esp32/fan/config/min_ms";       // retained uint32
constexpr const char* TOPIC_MINMS_SET     = "esp32/fan/config/min_ms/set";   // command uint32

// --- MQTT availability (LWT) ---
constexpr const char* TOPIC_AVAIL   = "esp32/availability";
constexpr const char* AVAIL_ONLINE  = "online";
constexpr const char* AVAIL_OFFLINE = "offline";

// --- Hysteresis defaults (runtime-changeable via MQTT) - from config.h ---
constexpr float FAN_T_ON_C_DEFAULT  = CFG_FAN_T_ON_C_DEFAULT;                 // turn ON at/above
constexpr float FAN_T_OFF_C_DEFAULT = CFG_FAN_T_OFF_C_DEFAULT;                // turn OFF at/below
static_assert(FAN_T_ON_C_DEFAULT > FAN_T_OFF_C_DEFAULT, "ON must be > OFF");

// --- DS18B20 publish (defaults; runtime values are MQTT-configurable) ---
constexpr float    DS18B20_DELTA_MIN_DEFAULT  = CFG_DS18B20_DELTA_MIN_DEFAULT;    // °C
constexpr uint32_t DS18B20_PUB_MIN_MS_DEFAULT = CFG_DS18B20_PUB_MIN_MS_DEFAULT;   // ms

// --- Timezone / DST (Europe/Warsaw) - from config.h  ---
constexpr long TZ_STD_OFFSET = CFG_TZ_STD_OFFSET;  // CET = UTC+1 (winter)
constexpr long TZ_DST_OFFSET = CFG_TZ_DST_OFFSET;  // CEST = UTC+2 (summer)
long tzOffsetSec = TZ_STD_OFFSET;     // will auto-correct after first NTP sync

// ============================================================================
// SECTION: GLOBAL OBJECTS
// ============================================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WiFiClient      netClient;
PubSubClient    mqtt(netClient);

// --- NTP ---
WiFiUDP  ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", tzOffsetSec, NTP_POLL_INTERVAL);

// --- DS18B20 ---
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

// ============================================================================
// SECTION: RUNTIME STATE
// ============================================================================
// --- Inbound HA values (as formatted strings for OLED) ---
String haTemp  = "--";
String haHumi  = "--";
String haPress = "--";

// --- Local sensor & control ---
float dsTempC       = NAN;
bool  fanOn         = false;
float fanOnC        = FAN_T_ON_C_DEFAULT;   // mutable via MQTT
float fanOffC       = FAN_T_OFF_C_DEFAULT;  // mutable via MQTT

enum class FanMode { AUTO, FORCED_ON, FORCED_OFF };
FanMode fanMode     = FanMode::AUTO;

// --- Last time (HH:MM) when any HA payload arrived (displayed on row 3, col 2) ---
String lastUpdateHHMM = "--:--";

// --- Redraw flag ---
volatile bool hasFreshData = false;

// --- Mutable values controlled via MQTT ---
float    ds18b20_delta_min  = DS18B20_DELTA_MIN_DEFAULT;
uint32_t ds18b20_pub_min_ms = DS18B20_PUB_MIN_MS_DEFAULT;

// --- Forward declarations (Arduino IDE safety) ---
static void subscribeTopics();
static void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================================
/* SECTION: HELPERS (formatting, time, publishing, control) */
// ============================================================================

/**
 * @brief Parse the first float from a payload and format with given precision.
 *        No left-padding is applied (OLED uses left-aligned columns).
 * @param payload  Input string (MQTT payload).
 * @param digits   Number of decimal places.
 * @return Formatted string or "--" if parsing failed.
 */
static String fmtNumber(const String& payload, int digits) {
  float v = NAN;
  if (sscanf(payload.c_str(), "%f", &v) == 1 && isfinite(v)) return String(v, digits);
  return "--";
}

/**
 * @brief Get local HH:MM from NTP (or "--:--" until time is set).
 */
static String nowHHMM() {
  if (!timeClient.isTimeSet()) return "--:--";
  const String hhmmss = timeClient.getFormattedTime(); // "HH:MM:SS"
  return hhmmss.substring(0, 5);
}

/**
 * @brief Remember "last updated" time for inbound MQTT payloads.
 */
inline void stampLastUpdate() { lastUpdateHHMM = nowHHMM(); }

/**
 * @brief Validate DS18B20 reading: rejects disconnected (-127 °C) and power-on default (85 °C).
 */
inline bool isValidDs18(float t) {
  return isfinite(t) && t > -55.0f && t < 125.0f && fabsf(t - 85.0f) > 0.001f;
}

/**
 * @brief Publish fan ON/OFF state (retained).
 */
static void publishFanState() {
  mqtt.publish(TOPIC_FAN_OUT, fanOn ? "ON" : "OFF", true);
}

/**
 * @brief Publish current fan mode (retained).
 */
static void publishFanMode() {
  const char* m = (fanMode == FanMode::AUTO) ? "AUTO" : (fanMode == FanMode::FORCED_ON ? "ON" : "OFF");
  mqtt.publish(TOPIC_FAN_MODE_STATE, m, true);
}

/**
 * @brief Publish current hysteresis thresholds (retained).
 */
static void publishFanConfig() {
  char b1[16], b2[16];
  dtostrf(fanOnC,  0, 2, b1);
  dtostrf(fanOffC, 0, 2, b2);
  mqtt.publish(TOPIC_ON_C_STATE,  b1, true);
  mqtt.publish(TOPIC_OFF_C_STATE, b2, true);
}

/**
 * @brief Publish current DS18B20 publish config (delta and min interval).
 */
static void publishPubConfig() {
  char b1[16], b2[16];
  dtostrf(ds18b20_delta_min, 0, 2, b1);                // e.g. "0.25"
  snprintf(b2, sizeof(b2), "%lu", (unsigned long)ds18b20_pub_min_ms); // e.g. "60000"
  mqtt.publish(TOPIC_DELTA_C_STATE, b1, true);
  mqtt.publish(TOPIC_MINMS_STATE,  b2, true);
}

/**
 * @brief Publish DS18B20 temperature if last cached value is valid (retained).
 */
static void publishDs18IfValid() {
  static float    lastPubVal = NAN;   // last published value
  static uint32_t lastPubMs  = 0;     // millis() of last publish

  if (!isValidDs18(dsTempC)) return;

  const uint32_t now   = millis();
  const bool timeOk    = (now - lastPubMs) >= ds18b20_pub_min_ms;
  const bool first     = isnan(lastPubVal);
  const bool deltaOk   = first || fabsf(dsTempC - lastPubVal) >= ds18b20_delta_min;

  if (first || deltaOk || timeOk) {
    char tb[16];
    dtostrf(dsTempC, 0, 2, tb);
    mqtt.publish(TOPIC_DS18_OUT, tb, true);  // retained
    lastPubVal = dsTempC;
    lastPubMs  = now;
  }
}

/**
 * @brief Decide target fan state (mode + hysteresis) and drive GPIO if it changed.
 */
static void applyFanControl() {
  bool targetOn = fanOn;

  if (fanMode == FanMode::FORCED_ON)       targetOn = true;
  else if (fanMode == FanMode::FORCED_OFF) targetOn = false;
  // AUTO with hysteresis (only when sensor value is valid)
  else if (isValidDs18(dsTempC)) {
    if (!fanOn && dsTempC >= fanOnC)      targetOn = true;
    else if (fanOn && dsTempC <= fanOffC) targetOn = false;
  }

  if (targetOn != fanOn) {
    fanOn = targetOn;
    digitalWrite(PIN_FAN, fanOn ? HIGH : LOW);
    publishFanState();
  }
}

/**
 * @brief Read DS18B20 (blocking), update cache on valid value, publish, and apply fan control.
 */
static void readDs18b20() {
  sensors.requestTemperatures();                // blocking; OK at current sampling cadence
  float t = sensors.getTempCByIndex(0);
  if (isValidDs18(t)) dsTempC = t;              // cache only valid reading
  publishDs18IfValid();                         // publishes last known good
  applyFanControl();
}

// ============================================================================
// SECTION: MQTT (subscribe, callback, connection with LWT)
// ============================================================================

/**
 * @brief Uppercase in-place (ASCII).
 */
static void to_upper_inplace(String& s) {
  for (size_t i = 0; i < s.length(); ++i) s.setCharAt(i, toupper((unsigned char)s[i]));
}

/**
 * @brief Subscribe to all inbound topics (HA values + control).
 */
static void subscribeTopics() {
  mqtt.subscribe(TOPIC_TEMP_IN);
  mqtt.subscribe(TOPIC_HUMI_IN);
  mqtt.subscribe(TOPIC_PRESS_IN);
  mqtt.subscribe(TOPIC_FAN_CMD);
  mqtt.subscribe(TOPIC_ON_C_SET);
  mqtt.subscribe(TOPIC_OFF_C_SET);
  mqtt.subscribe(TOPIC_DELTA_C_SET);
  mqtt.subscribe(TOPIC_MINMS_SET);
}

/**
 * @brief MQTT callback: HA → values, mode changes, threshold updates.
 */
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg; msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  if      (strcmp(topic, TOPIC_TEMP_IN)  == 0) { haTemp  = fmtNumber(msg, 1); stampLastUpdate(); }
  else if (strcmp(topic, TOPIC_HUMI_IN)  == 0) { haHumi  = fmtNumber(msg, 0); stampLastUpdate(); }
  else if (strcmp(topic, TOPIC_PRESS_IN) == 0) { haPress = fmtNumber(msg, 0); stampLastUpdate(); }

  else if (strcmp(topic, TOPIC_FAN_CMD) == 0) {
    String m = msg; to_upper_inplace(m);
    if      (m == "AUTO") { fanMode = FanMode::AUTO;       publishFanMode(); applyFanControl(); }
    else if (m == "ON")   { fanMode = FanMode::FORCED_ON;  publishFanMode(); applyFanControl(); }
    else if (m == "OFF")  { fanMode = FanMode::FORCED_OFF; publishFanMode(); applyFanControl(); }
    // unknown payloads are ignored
  }
  else if (strcmp(topic, TOPIC_ON_C_SET) == 0) {
    float v = msg.toFloat();
    if (!(v > fanOffC)) v = fanOffC + 0.5f;   // enforce ON > OFF (0.5 °C gap)
    fanOnC = v;
    publishFanConfig();
    if (fanMode == FanMode::AUTO) applyFanControl();
  }
  else if (strcmp(topic, TOPIC_OFF_C_SET) == 0) {
    float v = msg.toFloat();
    if (!(v < fanOnC)) v = fanOnC - 0.5f;     // enforce OFF < ON (0.5 °C gap)
    fanOffC = v;
    publishFanConfig();
    if (fanMode == FanMode::AUTO) applyFanControl();
  }
  else if (strcmp(topic, TOPIC_DELTA_C_SET) == 0) {
    float v = msg.toFloat();
    // Safe range: 0.05°C .. 5.0°C
    if (!isfinite(v) || v < 0.05f) v = 0.05f;
    if (v > 5.0f) v = 5.0f;
    ds18b20_delta_min = v;
    publishPubConfig();  // confirm new retained state
  }
  else if (strcmp(topic, TOPIC_MINMS_SET) == 0) {
    uint32_t v = (uint32_t) strtoul(msg.c_str(), nullptr, 10);
    // Safe range: 5 s .. 1 h (in ms)
    if (v < 5000UL) v = 5000UL;
    if (v > 3600000UL) v = 3600000UL;
    ds18b20_pub_min_ms = v;
    publishPubConfig();
  }

  hasFreshData = true;
}

/**
 * @brief Ensure MQTT is connected; connect with LWT, subscribe, and republish retained state.
 */
static void ensureMqtt() {
  static uint32_t backoff = 2000;
  while (!mqtt.connected()) {
    const uint64_t mac = ESP.getEfuseMac();
    const String cid = "ESP32Display-"
                    + String((uint32_t)(mac >> 32), HEX)
                    + String((uint32_t)(mac & 0xFFFFFFFF), HEX);
    if (mqtt.connect(cid.c_str(), CFG_MQTT_USER, CFG_MQTT_PASSWORD,
                     TOPIC_AVAIL, 0, true, AVAIL_OFFLINE)) {

      mqtt.publish(TOPIC_AVAIL, AVAIL_ONLINE, true);
      subscribeTopics();
      publishFanMode();
      publishFanConfig();
      publishFanState();
      publishDs18IfValid();
      publishPubConfig();
      backoff = 2000; // reset after success

    } else {
      delay(backoff);
      backoff = (backoff < 60000U) ? (backoff * 2U) : 60000U; // max 60s
    }
  }
}

// ============================================================================
// SECTION: DISPLAY (SSD1306 UI)
// ============================================================================

/**
 * @brief Draw the 3-line UI with two left-aligned columns.
 */
static void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);

// --- Row 1: MQTT temp + humidity ---
  display.setCursor(COL1_X, ROW_Y0);
  display.print(haTemp); display.write((uint8_t)248); display.print("C"); // 248 = '°' with cp437
  display.setCursor(COL2_X, ROW_Y0);
  display.print(haHumi); display.print("%");

// --- Row 2: MQTT pressure + FAN state ---
  display.setCursor(COL1_X, ROW_Y1);
  display.print(haPress); display.print(" hPa");
  display.setCursor(COL2_X, ROW_Y1);
  display.print("FAN "); display.print(fanOn ? "ON" : "OFF");

// --- Row 3: local DS18B20 (integer) + LAST MQTT update time (HH:MM) ---
  display.setCursor(COL1_X, ROW_Y2);
  if (isValidDs18(dsTempC)) { display.print((int)round(dsTempC)); display.write((uint8_t)248); display.print("C"); }
  else                   { display.print("--C"); }
  display.setCursor(COL2_X, ROW_Y2);
  display.print(lastUpdateHHMM);

  display.display();
}

// ============================================================================
// SECTION: INIT HELPERS (Wi-Fi, MQTT)
// ============================================================================

/**
 * @brief Connect to Wi-Fi in STA mode (blocking until connected).
 */
static void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(CFG_WIFI_HOSTNAME);
  WiFi.setSleep(true); // enable Power Save regardless of state
  WiFi.begin(CFG_WIFI_SSID, CFG_WIFI_PASSWORD);

  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000UL) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    int rssi = WiFi.RSSI();
    wifi_power_t pwr = (rssi > -45) ? WIFI_POWER_2dBm :
                       (rssi > -60) ? WIFI_POWER_5dBm :
                       (rssi > -70) ? WIFI_POWER_8_5dBm : WIFI_POWER_11dBm;
    WiFi.setTxPower(pwr);
  }
}

/**
 * @brief Configure MQTT server and callback.
 */
static void initMQTT() {
  mqtt.setServer(CFG_MQTT_HOST, CFG_MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);   // default 15s -> 60s: fewer PINGs
  mqtt.setSocketTimeout(5);
}

// --- DST helpers for EU (last Sunday of Mar/Oct; switch at 01:00 UTC) ---
static int weekdayYMD(int y, int m, int d) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y -= 1;
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7; // 0=Sun..6=Sat
}

// Return day-of-month of the last Sunday in given month (1..12)
static int lastSunday(int year, int month) {
  int w = weekdayYMD(year, month, 31);   // 0=Sunday..6=Saturday
  return 31 - w;                         // Sunday->31, Monday->30, Tuesday->29, ...
}

// Decide if DST is active for Europe/Warsaw at given UTC epoch
static bool isDstEU_Warsaw(time_t utcEpoch) {
  struct tm t; gmtime_r(&utcEpoch, &t);
  int year=t.tm_year+1900, mon=t.tm_mon+1, mday=t.tm_mday, hour=t.tm_hour;
  const int marLast = lastSunday(year, 3), octLast = lastSunday(year, 10);

  if (mon < 3 || mon > 10) return false;          // Jan-Feb, Nov-Dec => CET
  if (mon > 3 && mon < 10) return true;           // Apr-Sep => CEST
  if (mon == 3) {                                  // March: from last Sunday 01:00 UTC
    if (mday > marLast) return true;
    if (mday < marLast) return false;
    return hour >= 1;
  } else {                                         // October: until last Sunday 01:00 UTC
    if (mday < octLast) return true;
    if (mday > octLast) return false;
    return hour < 1;
  }
}

// Update NTPClient offset if DST boundary crossed
static void updateDstOffsetIfNeeded() {
  if (!timeClient.isTimeSet()) return;

  // NTPClient's epoch includes its current offset; get back to UTC:
  time_t localEpoch = (time_t) timeClient.getEpochTime();
  time_t utcEpoch   = localEpoch - tzOffsetSec;

  const bool dst = isDstEU_Warsaw(utcEpoch);
  const long wanted = dst ? TZ_DST_OFFSET : TZ_STD_OFFSET;

  if (wanted != tzOffsetSec) {
    tzOffsetSec = wanted;
    timeClient.setTimeOffset(tzOffsetSec);
  }
}

// ============================================================================
// SECTION: ARDUINO FRAMEWORK (ESP32): setup / loop
// ============================================================================

void setup() {
  Serial.begin(115200);

// --- I²C and OLED ---
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.cp437(true);         // enable Code Page 437 (for the '°' character at 248)
  display.setTextWrap(false);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.display();

// --- MOSFET gate ---
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_FAN, LOW);  // OFF at boot

// --- DS18B20 ---
  sensors.begin();

// --- Wi-Fi + time + MQTT ---
  initWiFi();
  timeClient.begin();
  timeClient.update();
  updateDstOffsetIfNeeded();
  initMQTT();

// --- Initial draw ---
  lastUpdateHHMM = "--:--";
  hasFreshData = true;
  drawScreen();
}

void loop() {
  ensureMqtt();
  mqtt.loop();

// --- Keep NTP fresh (local time flows continuously between polls) ---
  timeClient.update();
  updateDstOffsetIfNeeded();

// --- DS18B20 every DS18B20_SAMPLE_MS ---
  static uint32_t lastTempMs = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastTempMs >= DS18B20_SAMPLE_MS) {
    lastTempMs = nowMs;
    readDs18b20();
    hasFreshData = true;
  }

// --- Redraw when data changed, or as a safety every REDRAW_INTERVAL_MS ---
  static uint32_t lastRedrawMs = 0;
  if (hasFreshData || (nowMs - lastRedrawMs >= REDRAW_INTERVAL_MS)) {
    drawScreen();
    lastRedrawMs = nowMs;
    hasFreshData = false;
  }

  delay(10);
}
