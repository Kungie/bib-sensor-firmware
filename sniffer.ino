#include <WiFi.h>
#include <WiFiClient.h>
#include <NimBLEDevice.h>
#include <esp_task_wdt.h>

// --------- SETTINGS ----------
#include "config.h"
const char* SENSOR_ID     = "Sniffer-3";

const int   RSSI_FAR         = -90;

const int MEASUREMENTS_BEFORE_POST = 3;
const int SUB_SCANS                = 5;
const int SUB_SCAN_SECS            = 3;
const int REST_MS                  = 20000;
const int SIGNAL_CAP               = 1000;

const uint32_t MIN_HEAP_BYTES = 20000;
const int WDT_TIMEOUT_SEC = 180;

// ---------- GLOBALS ----------
float    sum_occupancy  = 0.0f;
int      meas_count     = 0;
volatile float g_weight       = 0.0f;
volatile int   g_signal_count = 0;
NimBLEScan* pScan = nullptr;

portMUX_TYPE ble_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------- BLE CALLBACK ----------
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    portENTER_CRITICAL(&ble_mux);
    if (g_signal_count >= SIGNAL_CAP) {
      portEXIT_CRITICAL(&ble_mux);
      return;
    }
    g_signal_count++;
    portEXIT_CRITICAL(&ble_mux);

    int rssi = dev->getRSSI();
    if (rssi < RSSI_FAR) return;

    portENTER_CRITICAL(&ble_mux);
    g_weight += 1.0f;
    portEXIT_CRITICAL(&ble_mux);
  }
};

ScanCallbacks scanCB;

// ---------- HEAP MONITOR ----------
void logHeap(const char* tag) {
  Serial.print("[HEAP] "); Serial.print(tag);
  Serial.print(" | Free: "); Serial.print(ESP.getFreeHeap());
  Serial.print(" | MinEver: "); Serial.print(ESP.getMinFreeHeap());
  Serial.print(" | MaxAlloc: "); Serial.println(ESP.getMaxAllocHeap());
}

// ---------- BLE SCAN ----------
float bleOccupancy() {
  float total_weight  = 0.0f;
  int   total_signals = 0;

  for (int i = 0; i < SUB_SCANS; i++) {
    portENTER_CRITICAL(&ble_mux);
    g_weight       = 0.0f;
    g_signal_count = 0;
    portEXIT_CRITICAL(&ble_mux);

    Serial.print("  [BLE] Sub-scan #"); Serial.println(i + 1);

    NimBLEScanResults results = pScan->getResults(SUB_SCAN_SECS * 1000, false);
    pScan->clearResults();
    esp_task_wdt_reset();

    portENTER_CRITICAL(&ble_mux);
    float w_snapshot = g_weight;
    int   c_snapshot = g_signal_count;
    portEXIT_CRITICAL(&ble_mux);

    Serial.print("  [BLE] Signals seen: "); Serial.print(c_snapshot);
    Serial.print(" | Weight: ");            Serial.println(w_snapshot, 2);

    total_weight  += w_snapshot;
    total_signals += c_snapshot;

    delay(100);
  }

  float avg_weight = total_weight / SUB_SCANS;

  Serial.println("  [BLE] ---- Scan Summary ----");
  Serial.print("  [BLE] Total signals: "); Serial.println(total_signals);
  Serial.print("  [BLE] Total weight:  "); Serial.println(total_weight, 2);
  Serial.print("  [BLE] Avg weight:    "); Serial.println(avg_weight, 2);
  Serial.println("  [BLE] --------------------------");
  logHeap("after BLE scan");

  return avg_weight;
}

// ---------- WIFI CONNECT ----------
bool wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Already connected");
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");

  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(" OK");
      Serial.print("[WiFi] IP: ");   Serial.println(WiFi.localIP());
      Serial.print("[WiFi] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
      return true;
    }
    if (i % 5 == 0) {
      int s = WiFi.status();
      Serial.print("\n[WiFi] Status="); Serial.print(s);
      Serial.print(" (");
      switch(s) {
        case WL_IDLE_STATUS:     Serial.print("IDLE");         break;
        case WL_NO_SSID_AVAIL:   Serial.print("NO_SSID");      break;
        case WL_SCAN_COMPLETED:  Serial.print("SCAN_DONE");    break;
        case WL_CONNECT_FAILED:  Serial.print("CONN_FAILED");  break;
        case WL_CONNECTION_LOST: Serial.print("CONN_LOST");    break;
        case WL_DISCONNECTED:    Serial.print("DISCONNECTED"); break;
        default:                 Serial.print("UNKNOWN");      break;
      }
      Serial.print(")");
    }
    esp_task_wdt_reset();
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] FAILED");
  return false;
}

// ---------- POST ----------
bool postData(float occupancy) {
  WiFiClient client;
  client.setTimeout(10000);

  Serial.println("[API] Connecting to server...");

  esp_task_wdt_reset();
  bool connected = client.connect(SERVER_HOST, SERVER_PORT);
  esp_task_wdt_reset();

  if (!connected) {
    Serial.println("[API] Could not connect to server");
    client.stop();
    return false;
  }

  Serial.println("[API] Connected");

  String body = "{\"average_weight\":" + String(occupancy, 2) +
                ",\"sensor_id\":\"" + SENSOR_ID + "\"}";

  client.print("POST "); client.print(SERVER_PATH); client.println(" HTTP/1.1");
  client.print("Host: "); client.println(SERVER_HOST);
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(body.length());
  client.println("Connection: close");
  client.println();
  client.print(body);

  esp_task_wdt_reset();

  unsigned long t = millis();
  while (!client.available() && millis() - t < 10000) {
    esp_task_wdt_reset();
    delay(10);
  }

  String status = client.readStringUntil('\n');
  client.stop();
  Serial.print("[API] Response: "); Serial.println(status);
  return status.indexOf("200") > 0;
}

// ---------- HEAP CHECK ----------
void checkHeap() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap  = ESP.getMinFreeHeap();
  if (freeHeap < MIN_HEAP_BYTES || minHeap < 10000) {
    Serial.printf("[SYSTEM] Heap critical (free=%lu, min=%lu), rebooting\n", freeHeap, minHeap);
    delay(500);
    ESP.restart();
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== bib. Sensor Starting ===");
  logHeap("boot");

  esp_err_t err = esp_task_wdt_add(NULL);
  if (err == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_config_t wdt_config = {
      .timeout_ms    = (uint32_t)(WDT_TIMEOUT_SEC * 1000),
      .idle_core_mask = 0,
      .trigger_panic  = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
  }
  Serial.printf("[WDT] Watchdog: %ds\n", WDT_TIMEOUT_SEC);

  WiFi.mode(WIFI_OFF);
  delay(300);

  NimBLEDevice::init("bib-sniffer");
  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCB, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(80);
  pScan->setDuplicateFilter(true);

  Serial.println("[BLE] Ready");
  logHeap("after BLE init");
}

// ---------- LOOP ----------
void loop() {
  esp_task_wdt_reset();
  checkHeap();

  Serial.println("[BLE] Starting scan...");
  float w = bleOccupancy();
  sum_occupancy += w;
  meas_count++;

  Serial.print("[LOOP] Measurement "); Serial.print(meas_count);
  Serial.print("/"); Serial.println(MEASUREMENTS_BEFORE_POST);

  if (meas_count >= MEASUREMENTS_BEFORE_POST) {
    float avg = sum_occupancy / meas_count;
    Serial.print("[LOOP] Avg weight to send: "); Serial.println(avg, 1);

    pScan->stop();
    pScan->clearResults();
    delay(500);
    logHeap("before WiFi");

    if (wifiConnect()) {
      bool ok = postData(avg);
      Serial.println(ok ? "[API] POST successful" : "[API] POST failed");
    }

    static int cycle_count = 0;
    cycle_count++;
    if (cycle_count >= 50) {
    Serial.println("[SYSTEM] Scheduled maintenance reboot");
    delay(500);
    ESP.restart();
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(2000);
    logHeap("after WiFi off");

    unsigned long ws = millis();
    while (ESP.getFreeHeap() < 50000 && millis() - ws < 30000) {
      esp_task_wdt_reset();
      delay(500);
    }
    logHeap("after heap recovery");

    sum_occupancy = 0.0f;
    meas_count    = 0;
  }

  Serial.println("[LOOP] Resting 20s...");
  for (int i = 0; i < 40; i++) {
    esp_task_wdt_reset();
    delay(500);
  }
}