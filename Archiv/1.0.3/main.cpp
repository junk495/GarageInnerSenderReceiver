// =====================================================================================
// GarageLuefterregler (code2) - V4.18 (Angepasst für Sender mit Batteriedaten)
// -------------------------------------------------------------------------------------
// Letzte Änderung: 27. Juni 2025
// Hardware:        AZ-Delivery ESP32 D1 Mini
// Funktion:        Ein Garagen-Controller, der Daten via ESP-NOW empfängt, das
//                  Raumklima misst, einen Lüfter sowie ein Relais steuert und
//                  konsolidierte Daten via LoRa weiterleitet.
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
#define SEND_INTERVAL_S (1 * 60)

// LoRa Pins (EByte E220)
#define LORA_TX 17
#define LORA_RX 16
#define LORA_M0 13
#define LORA_M1 14
#define LORA_AUX 27

#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 21

bool debug1 = true;
bool debug2 = true;

// Globale Instanzen
Adafruit_BME280 bme;
volatile bool immediateSend = false;
LoRa_E220 e220ttl(&Serial2, LORA_AUX, LORA_M0, LORA_M1);
Ticker sendTicker;

// ---------------- Strukturen ----------------
// GEÄNDERT: Felder für Batteriespannung und -strom hinzugefügt, um zum Sender zu passen
struct SensorMessage {
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
};

// GEÄNDERT: Felder für Batteriespannung und -strom hinzugefügt, um zum Sender zu passen
struct FingerEvent {
  uint8_t type;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
};

// GEÄNDERT: Feld für Batteriespannung hinzugefügt
struct __attribute__((packed)) LoRaPayload {
  uint16_t messageID;
  float temperatureInnen, humidityInnen, absHumidityInnen;
  float temperatureAussen, humidityAussen, absHumidityAussen;
  bool  fanOn;
  uint8_t torStatus, fingerID, confidence;
  bool  fingerEventValid;
  uint16_t actionID;
  float batteryVoltage; // NEU
};

// ---------------- Globale Werte ----------------
float letzteTempAussen = 0.0;
float letzteRH_Aussen = 0.0;
float letzteAbsAussen = 0.0;
uint8_t letzteTorStatus = 0;
float letzteBatteryVoltage = 0.0; // NEU

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
  p.batteryVoltage    = letzteBatteryVoltage; // GEÄNDERT: Batteriespannung wird dem Payload hinzugefügt

  ResponseStatus rs;
  int retries = 3;
  while (retries > 0) {
    rs = e220ttl.sendFixedMessage(0, 2, 23, &p, sizeof(p));
    if (rs.code == E220_SUCCESS) break;
    retries--;
    if (debug2) Serial.printf("[LORA] Retry %d, Status: %s %s\n", 3-retries, rs.getResponseDescription().c_str(), getTimeStamp().c_str());
    delay(100);
  }

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
      Serial.printf(" > Fan=%s, Tor=%d, Vbat=%.2f V\n", fan ? "On" : "Off", letzteTorStatus, p.batteryVoltage); // GEÄNDERT: Vbat zur Ausgabe hinzugefügt
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
  
  // Lüfter an: Nur wenn es drinnen feuchter ist UND das Tor nicht ganz geschlossen ist (also auf oder teiloffen)
  if (!fanState && feuchteBedingungErfuellt && letzteTorStatus != 0) {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
      if (debug1) Serial.printf("[FAN] Lüfter ein geschaltet %s\n", getTimeStamp().c_str());
  }
  // Lüfter aus: Wenn es draußen feuchter ist ODER das Tor geschlossen ist
  else if (fanState && (!feuchteBedingungErfuellt || letzteTorStatus == 0)) {
      digitalWrite(FAN_PIN, LOW);
      fanState = false;
      if (debug1) Serial.println(getTimeStamp() + " [FAN] Lüfter ausgeschaltet (Bedingung nicht erfüllt oder Tor geschlossen).");
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
    letzteBatteryVoltage = m.batteryVoltage; // GEÄNDERT: Batteriespannung wird gespeichert
    receivedActionID = 0;
    fingerEventReceived = false;
  } else if (len == sizeof(FingerEvent)) {
    FingerEvent fe;
    memcpy(&fe, data, sizeof(fe));
    if (fe.type == 99) {
      letzteFingerID = fe.fingerID;
      letzteConfidence = fe.confidence;
      receivedActionID = fe.actionID;
      letzteTorStatus = fe.torStatus;
      letzteBatteryVoltage = fe.batteryVoltage; // GEÄNDERT: Batteriespannung wird gespeichert
      fingerEventReceived = true;
      immediateSend = true;
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
    cfg.ADDL = 2;
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
  if (relayActive && (millis() - relayStartTime >= RELAY_ON_DURATION)) {
    digitalWrite(RELAY_PIN, LOW);
    relayActive = false;
    if (debug1) Serial.println(getTimeStamp() + " [RELAY] Relais deaktiviert.");
  }
  
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