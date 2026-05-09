#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =====================================================================================
//                              Konfiguration
// =====================================================================================
// Diese Datei enthält alle zentralen Einstellungen und Pin-Definitionen
// für den GarageLuefterregler (code2).
// =====================================================================================

#include "secrets.h" // Lädt den geheimen ESP-NOW Schlüssel

// =====================================================================================
//                          WICHTIGE SICHERHEITSEINSTELLUNG
// =====================================================================================
// Trage hier die MAC-Adresse deines Senders (code1c) ein.
// Nur Nachrichten von dieser Adresse werden akzeptiert.
// Du findest die MAC-Adresse in der seriellen Ausgabe von code1c beim Start.
static const uint8_t known_sender_mac[] = {0xDC, 0x06, 0x75, 0xF9, 0x85, 0xE4}; 
// =====================================================================================


// ---------------- Einstellungen ----------------
#define ESPNOW_CHANNEL 6
#define FAN_PIN 23
#define RELAY_PIN 26
#define DIFFERENZ_SCHWELLE 1.0
#define GARAGE_FINGER_ACTION_ID 3250
#define SEND_INTERVAL_S (15 * 60)

// LoRa Pins (EByte E220)
#define LORA_TX 17
#define LORA_RX 16
#define LORA_M0 13
#define LORA_M1 14
#define LORA_AUX 27

// I2C Pins für den BME280 Sensor
#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 21

// Debug-Schalter
#define DEBUG1 true
#define DEBUG2 true

// Korrekturwerte für den internen BME280-Sensor
#define BME_INNER_TEMP_OFFSET_C      -2.6
#define BME_INNER_HUM_OFFSET_PERCENT  +9

// Einstellungen für den manuellen Lüftertaster
#define FAN_BUTTON_PIN 18
#define MANUAL_FAN_DURATION_MINUTES 15

// Typen für die empfangene SensorMessage
#define SENSOR_MESSAGE_TYPE_NORMAL 0
#define SENSOR_MESSAGE_TYPE_CONTROL_UPDATE 1

#endif // CONFIG_H
