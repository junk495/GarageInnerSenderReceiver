#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>

// =====================================================================================
//                              Datenstrukturen
// =====================================================================================
// Diese Datei enthält die Definitionen aller Datenstrukturen, die im Projekt
// verwendet werden (ESP-NOW und LoRa).
// =====================================================================================

// Struktur für empfangene Sensordaten via ESP-NOW
struct SensorMessage {
  uint8_t type;
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
  uint8_t wakeupCause;
};

// Struktur für empfangene Fingerabdruck-Events via ESP-NOW
struct FingerEvent {
  uint8_t type = 99;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
};

// Struktur für die zu sendenden LoRa-Daten
struct __attribute__((packed)) LoRaPayload {
  uint16_t messageID;
  float temperatureInnen, humidityInnen, absHumidityInnen;
  float temperatureAussen, humidityAussen, absHumidityAussen;
  bool  fanOn;
  uint8_t torStatus, fingerID, confidence;
  bool  fingerEventValid;
  uint16_t actionID;
  float batteryVoltage;
  uint8_t wakeupCause; // NEU: Feld für den Weckgrund hinzugefügt
};

// Struktur für empfangene LoRa-Befehle (z.B. vom MQTT-Gateway)
struct __attribute__((packed)) CommandPayload {
  uint8_t command; // z.B. 1 = TOGGLE_RELAY
};

#endif // STRUCTURES_H