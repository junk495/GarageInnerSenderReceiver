#include "espnow_handler.h"
#include "globals.h"
#include "structures.h"
#include "config.h"
#include "utils.h"
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <string.h>

// Initialisiert ESP-NOW für den Empfang
void init_espnow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    if (DEBUG1) Serial.println("ESP-NOW Init fehlgeschlagen!");
    while(1);
  }
  
  if (esp_now_set_pmk((const uint8_t *)pmk_key_str) != ESP_OK) {
    if (DEBUG1) Serial.println("ESP-NOW PMK setzen fehlgeschlagen!");
    while(1);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, known_sender_mac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = true;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    if (DEBUG1) Serial.println("Fehler beim Hinzufügen des Sende-Peers!");
    while(1);
  }

  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    if (DEBUG1) Serial.println("ESP-NOW Callback-Registrierung fehlgeschlagen!");
    while(1);
  }
}

// Callback-Funktion, die bei eingehenden ESP-NOW Nachrichten aufgerufen wird
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  // Sicherheitsprüfung: Nur Nachrichten vom bekannten Sender akzeptieren.
  if (memcmp(mac, known_sender_mac, 6) != 0) {
    if (DEBUG1) {
        char macStr[18];
        sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Serial.printf("WARNUNG: Nachricht von unbekannter MAC-Adresse %s ignoriert.\n", macStr);
    }
    return;
  }

  static unsigned long lastReceiveTime = 0;
  if (millis() - lastReceiveTime < 500) return;
  lastReceiveTime = millis();

  if (len == sizeof(SensorMessage)) {
    SensorMessage m; 
    memcpy(&m, data, sizeof(m));
    
    if (DEBUG1) {
        Serial.println(getTimeStamp() + " [ESP-NOW] SensorMessage empfangen:");
        // NEU: Debug-Ausgabe um 'Cause' erweitert
        Serial.printf("  > Typ: %d, Cause: %d, Temp: %.1f, Feuchte: %.1f, Tor: %d, Vbat: %.2fV\n", m.type, m.wakeupCause, m.temperature, m.humidity, m.torStatus, m.batteryVoltage);
    }

    letzteTempAussen = m.temperature;
    letzteRH_Aussen = m.humidity;
    letzteAbsAussen = m.absoluteHumidity;
    letzteTorStatus = m.torStatus;
    letzteBatteryVoltage = m.batteryVoltage;
    letzteWakeupCause = m.wakeupCause; // NEU: Weckgrund speichern
    
    receivedActionID = 0;
    fingerEventReceived = false;
    if (m.type == SENSOR_MESSAGE_TYPE_CONTROL_UPDATE) {
      immediateSend = true;
    }
  } else if (len == sizeof(FingerEvent)) {
    FingerEvent fe;
    memcpy(&fe, data, sizeof(fe));

    if (DEBUG1) {
        Serial.println(getTimeStamp() + " [ESP-NOW] FingerEvent empfangen:");
        Serial.printf("  > Typ: %d, FingerID: %d, Konfidenz: %d, ActionID: %d, Tor: %d, Vbat: %.2fV\n", fe.type, fe.fingerID, fe.confidence, fe.actionID, fe.torStatus, fe.batteryVoltage);
    }

    if (fe.type == 99) {
      letzteFingerID = fe.fingerID;
      letzteConfidence = fe.confidence;
      receivedActionID = fe.actionID;
      letzteTorStatus = fe.torStatus;
      letzteBatteryVoltage = fe.batteryVoltage;
      fingerEventReceived = true;
      immediateSend = true;
    }
  }
}