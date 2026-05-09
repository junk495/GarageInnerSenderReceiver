// =====================================================================================
// GarageLuefterregler (code2) - V4.17
// -------------------------------------------------------------------------------------
// Letzte Änderung: 26. Juni 2025, 20:05
// Hardware:        AZ-Delivery ESP32 D1 Mini
// Funktion:        Ein Garagen-Controller, der Daten via ESP-NOW empfängt, das
//                  Raumklima misst, einen Lüfter sowie ein Relais steuert und
//                  konsolidierte Daten via LoRa weiterleitet.
// Logik:
// - Empfängt Außensensor- und Fingerabdruck-Daten via ESP-NOW.
// - Ein Finger-Event löst das Garagentor-Relais und eine sofortige LoRa-Sendung aus.
// - Vergleicht die absolute Feuchtigkeit (innen via BME280, außen via ESP-NOW) und
//   steuert damit einen Lüfter.
// - Sendet alle Status- und Sensordaten periodisch (alle 1 Minute) oder
//   ereignisgesteuert (Finger-Event) via LoRa.
// =====================================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "LoRa_E220.h"
#include <Ticker.h>

// ---------------- Einstellungen ----------------
#define ESPNOW_CHANNEL 6
#define FAN_PIN 23
#define RELAY_PIN 26 
#define DIFFERENZ_SCHWELLE 1.0
#define GARAGE_FINGER_ACTION_ID 3250
#define SEND_INTERVAL_S (1 * 60) // Sendeintervall auf 1 Minute geändert

// LoRa Pins (EByte E220)
#define LORA_TX 17  // ESP32 RX2
#define LORA_RX 16  // ESP32 TX2
#define LORA_M0 13
#define LORA_M1 14
#define LORA_AUX 27

// I2C-Pins auf die Standard-Hardware-Pins des ESP32 geändert.
#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 21

bool debug1 = true;
bool debug2 = true;

// HINWEIS: Dies ist die MAC-Adresse des Senders
uint8_t senderMAC[] = {0xB0, 0xB2, 0x1C, 0x96, 0xA4, 0x88};

// Globale Instanzen
Adafruit_BME280 bme;
volatile bool immediateSend = false;
LoRa_E220 e220ttl(&Serial2, LORA_AUX, LORA_M0, LORA_M1);
Ticker sendTicker;

// ---------------- Strukturen ----------------
struct SensorMessage {
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
};

struct FingerEvent {
  uint8_t type;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
};

struct __attribute__((packed)) LoRaPayload {
  uint16_t messageID;
  float temperatureInnen, humidityInnen, absHumidityInnen;
  float temperatureAussen, humidityAussen, absHumidityAussen;
  bool  fanOn;
  uint8_t torStatus, fingerID, confidence;
  bool  fingerEventValid;
  uint16_t actionID;
  float batteryVoltage;
};

// ---------------- Globale Werte ----------------
float letzteTempAussen = 0.0;
float letzteRH_Aussen = 0.0;
float letzteAbsAussen = 0.0;
uint8_t letzteTorStatus = 0;

float letzteInnenTemp = NAN;
float letzteInnenRH   = NAN;
float letzteInnenAbs  = NAN;

uint8_t letzteFingerID   = 0;
uint8_t letzteConfidence = 0;
bool    fingerEventReceived = false;
volatile uint16_t receivedActionID = 0;

bool fanState = false;
unsigned long lastSendTime = 0;
const unsigned long MIN_SEND_INTERVAL = 2000; // 2 Sekunden
uint16_t messageCounter = 0;

// Variablen für non-blocking Relais-Steuerung
unsigned long relayStartTime = 0;
bool relayActive = false;
const unsigned long RELAY_ON_DURATION = 1000; // 1 Sekunde

// ---------------- Hilfsfunktionen ----------------
String getTimeStamp() {
  unsigned long ms = millis();
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  seconds %= 60;
  minutes %= 60;
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu]", hours, minutes, seconds);
  return String(buffer);
}

// KORREKTUR: Die Berechnungsformel wurde an die korrekte Version aus code1c angepasst.
float calculateAbsoluteHumidity(float temperature, float humidity) {
  return (6.112 * pow(2.71828, (17.67 * temperature) / (temperature + 243.5)) * humidity * 2.1674) / (273.15 + temperature);
}

void readLocalSensors() {
  float t = 0, rh = 0;
  const int samples = 3;
  for (int i = 0; i < samples; i++) {
    t += bme.readTemperature();
    rh += bme.readHumidity();
    delay(10);
  }
  t /= samples;
  rh /= samples;
  if (isnan(t) || isnan(rh)) return;
  letzteInnenTemp = t;
  letzteInnenRH = rh;
  letzteInnenAbs = calculateAbsoluteHumidity(t, rh);
}

// ---------------- LoRa-Senden ----------------
void sendeLoraDaten(float tIn, float rhIn, float absIn, bool fan) {
  if (millis() - lastSendTime < MIN_SEND_INTERVAL) {
    if (debug2) Serial.println("[LORA] Senden übersprungen: Zu kurz nach letztem." + getTimeStamp());
    return;
  }

  // Warte, bis Modul bereit ist
  unsigned long startWait = millis();
  while (digitalRead(LORA_AUX) == LOW) {
    if (millis() - startWait > 1000) {
      if (debug2) Serial.println("[ERROR] LoRa-Modul ist dauerhaft beschäftigt!" + getTimeStamp());
      return;
    }
    delay(1);
  }

  if (debug2) Serial.println("[LORA] Modul ist bereit. Bereite Senden vor..." + getTimeStamp());
  LoRaPayload p{};
  p.messageID         = messageCounter++;
  p.temperatureInnen  = tIn;
  p.humidityInnen     = rhIn;
  p.absHumidityInnen  = absIn;
  p.temperatureAussen = letzteTempAussen;
  p.humidityAussen    = letzteRH_Aussen;
  p.absHumidityAussen = letzteAbsAussen;
  p.fanOn             = fan;
  p.torStatus         = letzteTorStatus;
  p.fingerEventValid  = fingerEventReceived;
  p.fingerID          = letzteFingerID;
  p.confidence        = letzteConfidence;
  p.actionID          = receivedActionID;
  
  ResponseStatus rs;
  int retries = 3;
  while (retries > 0) {
    // KORREKTUR: sendFixedMessage statt sendMessage, um zur Konfiguration des Moduls zu passen
    rs = e220ttl.sendFixedMessage(0, 2, 23, &p, sizeof(p));
    if (rs.code == E220_SUCCESS) break;
    retries--;
    if (debug2) Serial.printf("[LORA] Retry %d, Status: %s %s\n", 3-retries, rs.getResponseDescription().c_str(), getTimeStamp().c_str());
    delay(100);
  }
  
  // Warte auf Abschluss des Sendens
  startWait = millis();
  while (digitalRead(LORA_AUX) == LOW) {
    if (millis() - startWait > 2000) {
      if (debug2) Serial.println("[ERROR] LoRa-Senden nicht abgeschlossen!" + getTimeStamp());
      break;
    }
    delay(1);
  }

  if (debug2) {
    if (rs.code == E220_SUCCESS) {
      Serial.printf("[LORA] Sendebefehl erfolgreich abgesetzt. Inhalt: %s\n", getTimeStamp().c_str());
      Serial.printf(" > MessageID=%u\n", p.messageID);
      Serial.printf(" > Tin=%.1f, RHin=%.1f, Ain=%.1f\n", tIn, rhIn, absIn);
      Serial.printf(" > Tau=%.1f, RHa=%.1f, Aa=%.1f\n", letzteTempAussen, letzteRH_Aussen, letzteAbsAussen);
      Serial.printf(" > Fan=%s, Tor=%d\n", fan ? "On" : "Off", letzteTorStatus);
      if (fingerEventReceived) {
        Serial.printf(" > Finger-Event: ID=%d, Conf=%d, ActionID=%d\n", letzteFingerID, letzteConfidence, receivedActionID);
      }
    } else {
      Serial.printf("[ERROR] LoRa Sendefehler nach %d Versuchen: %s %s\n", 3-retries+1, rs.getResponseDescription().c_str(), getTimeStamp().c_str());
    }
  }
  if (rs.code == E220_SUCCESS) lastSendTime = millis();
  fingerEventReceived = false;
}

void sendLoraPeriodically() {
  readLocalSensors();
  if (isnan(letzteInnenTemp) || isnan(letzteInnenRH)) {
    return;
  }
  sendeLoraDaten(letzteInnenTemp, letzteInnenRH, letzteInnenAbs, fanState);
}

// ---------------- Lüfterlogik ----------------
void updateFan(float innenAbs, float aussenAbs) {
  if (isnan(innenAbs) || isnan(aussenAbs)) return;

  float diffAbs = innenAbs - aussenAbs;
  bool feuchteBedingungErfuellt = diffAbs > DIFFERENZ_SCHWELLE;
  
  if (!fanState && feuchteBedingungErfuellt && letzteTorStatus == 2) {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
      // KORREKTUR: Variable 'shouldBeOn' war nicht deklariert.
      if (debug1) Serial.printf("[FAN] Lüfter %sgeschaltet %s\n", "ein", getTimeStamp().c_str());
  }
  else if (fanState && (!feuchteBedingungErfuellt || letzteTorStatus != 2)) {
      digitalWrite(FAN_PIN, LOW);
      fanState = false;
      if (debug1) Serial.println(getTimeStamp() + " [FAN] Lüfter ausgeschaltet (Bedingung nicht erfüllt oder Tor geöffnet).");
  }
}

// ---------------- ESP-NOW Callback ----------------
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  static unsigned long lastReceiveTime = 0;
  if (millis() - lastReceiveTime < 500) return;
  lastReceiveTime = millis();

  if (len == sizeof(SensorMessage)) {
    SensorMessage m; memcpy(&m, data, sizeof(m));
    letzteTempAussen = m.temperature;
    letzteRH_Aussen = m.humidity;
    letzteAbsAussen = m.absoluteHumidity;
    letzteTorStatus = m.torStatus;
    receivedActionID = 0;
    fingerEventReceived = false;
    // Kein immediateSend: SensorMessage triggert keine Sendung
  } else if (len == sizeof(FingerEvent)) {
    FingerEvent fe;
    memcpy(&fe, data, sizeof(fe));
    if (fe.type == 99) {
      letzteFingerID = fe.fingerID;
      letzteConfidence = fe.confidence;
      receivedActionID = fe.actionID;
      letzteTorStatus = fe.torStatus;
      fingerEventReceived = true;
      immediateSend = true; // Nur Finger-Event triggert sofortige Sendung
    }
  }
}

// ---------------- Arduino Core-Funktionen ----------------
void setup() {
  Serial.begin(115200);
  delay(1000); 

  if (debug1) {
    Serial.println("\n" + getTimeStamp() + " [SETUP] GarageLuefterregler startet...");
    Serial.printf("[SETUP] Eigene MAC-Adresse: %s\n", WiFi.macAddress().c_str());
  }

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(LORA_AUX, INPUT);
  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!bme.begin(0x76)) {
    Serial.println(getTimeStamp() + " [ERROR] BME280 nicht gefunden!");
    while (1);
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println(getTimeStamp() + " [ERROR] ESP-NOW Init fehlgeschlagen!");
    while(1);
  }
  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    Serial.println(getTimeStamp() + " [ERROR] ESP-NOW Callback-Registrierung fehlgeschlagen!");
    while(1);
  }

  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  e220ttl.begin();

  ResponseStructContainer cfgContainer = e220ttl.getConfiguration();
  if (cfgContainer.status.code == E220_SUCCESS) {
    Configuration cfg = *(Configuration*)cfgContainer.data;
    cfg.ADDH = 0;
    cfg.ADDL = 2; // Eigene Adresse auf 2 setzen
    cfg.CHAN = 23;
    
    cfg.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

    auto status = e220ttl.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
    if (debug1) Serial.printf("%s [LoRa] Modul konfiguriert, Status: %s\n", getTimeStamp().c_str(), status.getResponseDescription().c_str());
    cfgContainer.close();
  } else {
    Serial.printf("%s [LoRa] Konfig lesen fehlgeschlagen: %s\n", getTimeStamp().c_str(), cfgContainer.status.getResponseDescription().c_str());
    cfgContainer.close();
    while (1);
  }

  sendTicker.attach(SEND_INTERVAL_S, sendLoraPeriodically);
  
  if (debug1) Serial.println(getTimeStamp() + " [SETUP] Initialisierung abgeschlossen.");
}

void loop() {
  // Relais-Steuerung (asynchron)
  if (relayActive && (millis() - relayStartTime >= RELAY_ON_DURATION)) {
    digitalWrite(RELAY_PIN, LOW);
    relayActive = false;
    if (debug1) Serial.println(getTimeStamp() + " [RELAY] Relais deaktiviert.");
  }
  
  // Verarbeitung von ESP-NOW Nachrichten
  if (immediateSend) {
    immediateSend = false;
    
    if (fingerEventReceived && receivedActionID == GARAGE_FINGER_ACTION_ID) {
      if (debug1) Serial.printf("%s [RELAY] Garagentor-Relais wird aktiviert.\n", getTimeStamp().c_str());
      digitalWrite(RELAY_PIN, HIGH);
      relayActive = true;
      relayStartTime = millis();
    }
    
    readLocalSensors();

    if (!isnan(letzteInnenTemp)) {
      updateFan(letzteInnenAbs, letzteAbsAussen);
      delay(100);
      sendeLoraDaten(letzteInnenTemp, letzteInnenRH, letzteInnenAbs, fanState);
    }

    receivedActionID = 0;
  }
}
