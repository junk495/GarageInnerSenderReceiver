#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>

// Struktur für empfangene Sensordaten via ESP-NOW
struct __attribute__((packed)) SensorMessage {
  uint8_t type;
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
  uint8_t wakeupCause;
  uint32_t messageCounter;
};

// Struktur für empfangene Fingerabdruck-Events via ESP-NOW
struct __attribute__((packed)) FingerEvent {
  uint8_t type = 99;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
  uint32_t messageCounter;
};

// Struktur für die zu sendenden LoRa-Daten (bereits gepackt)
struct __attribute__((packed)) LoRaPayload {
  uint16_t messageID;
  float temperatureInnen, humidityInnen, absHumidityInnen;
  float temperatureAussen, humidityAussen, absHumidityAussen;
  bool  fanOn;
  uint8_t torStatus, fingerID, confidence;
  bool  fingerEventValid;
  uint16_t actionID;
  float batteryVoltage;
  uint8_t wakeupCause;
};

// Struktur für empfangene LoRa-Befehle
struct __attribute__((packed)) CommandPayload {
  uint8_t command; 
};

#endif // STRUCTURES_H