#include "TDeckPlusBoard.h"
#include <esp_partition.h>
#include <esp_sleep.h>

void TDeckPlusBoard::begin() {
  // The peripheral rail must be up before anything else works.
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  delay(200);   // let the switched rail (display, radio, keyboard, GPS) come out of power-on reset

  // All SPI chip-selects idle HIGH before anyone touches the shared bus.
  pinMode(PIN_TFT_CS, OUTPUT);  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);   digitalWrite(PIN_SD_CS, HIGH);
  pinMode(P_LORA_NSS, OUTPUT);  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(PIN_SPI_MISO, INPUT_PULLUP);

  ESP32Board::begin();   // also Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL)

  pinMode(PIN_VBAT_ADC, INPUT);
  pinMode(PIN_USER_BTN, INPUT);
}

uint16_t TDeckPlusBoard::getBattMilliVolts() {
  analogReadResolution(12);
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT_ADC);
  return (uint16_t)((mv / 8) * 2);   // 1:2 divider
}

int TDeckPlusBoard::getBatteryPercent() {
  int mv = getBattMilliVolts();
  // piecewise-linear Li-ion discharge curve (resting voltage)
  static const int16_t pts[][2] = {
    {4150, 100}, {4050, 90}, {3970, 80}, {3900, 70}, {3840, 60}, {3790, 50},
    {3750, 40}, {3710, 30}, {3670, 20}, {3610, 10}, {3500, 5}, {3300, 0}
  };
  if (mv >= pts[0][0]) return 100;
  for (size_t i = 1; i < sizeof(pts) / sizeof(pts[0]); i++) {
    if (mv >= pts[i][0]) {
      int v0 = pts[i][0], p0 = pts[i][1], v1 = pts[i - 1][0], p1 = pts[i - 1][1];
      return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
    }
  }
  return 0;
}

bool TDeckPlusBoard::isCharging() {
  return getBattMilliVolts() > 4250;   // only true with USB power attached
}

bool TDeckPlusBoard::isRunningUnderLauncher() {
  // The bmorcelli Launcher installs itself in an app partition of subtype "test".
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, NULL) != NULL;
}

void TDeckPlusBoard::rebootToLauncher() {
  esp_sleep_enable_timer_wakeup(100000);   // 100 ms
  esp_deep_sleep_start();
}

static bool sawNmea(uint32_t baud, uint32_t ms) {
  Serial1.begin(baud, SERIAL_8N1, PIN_GPS_TX /*ESP RX*/, PIN_GPS_RX /*ESP TX*/);
  unsigned long t0 = millis();
  char prev = 0;
  while (millis() - t0 < ms) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (prev == '$' && c == 'G') { Serial1.end(); return true; }
      prev = c;
    }
    delay(5);
  }
  Serial1.end();
  return false;
}

void TDeckPlusBoard::switchGpsBaud() {
  Serial1.begin(9600, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
  Serial1.print("$PCAS01,3*1F\r\n");   // L76K: set baud to 38400 (harmless if it is already there)
  Serial1.flush();
  delay(60);
  Serial1.end();
  Serial1.begin(38400, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);   // MeshCore's start_gps() never reopens it
}

// Boot only (blocks up to ~2.7 s, before the watchdog starts).
uint32_t TDeckPlusBoard::prepareGps() {
  uint32_t found = 0;
  if (sawNmea(38400, 1300)) found = 38400;
  else if (sawNmea(9600, 1300)) found = 9600;
  if (found == 9600) switchGpsBaud();
  else Serial1.begin(38400, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
  return _gps_baud = found;
}
