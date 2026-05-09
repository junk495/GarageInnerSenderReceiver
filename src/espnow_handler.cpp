#include "espnow_handler.h"
#include "globals.h"
#include "structures.h"
#include "config.h"
#include "utils.h"
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <string.h>
#include <Preferences.h> // NEU: Notwendig für das preferences-Objekt

extern Preferences preferences; // NEU: Zugriff auf das globale Objekt ermöglichen

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
  // 1. Sicherheitsprüfung: Nur Nachrichten vom bekannten Sender akzeptieren.
  if (memcmp(mac, known_sender_mac, 6) != 0) {
    return;
  }

  uint32_t incomingCounter = 0;
  bool isSensorMessage = false;
  bool isFingerEvent = false;

  if (len == sizeof(SensorMessage)) {
    isSensorMessage = true;
    SensorMessage m; 
    memcpy(&m, data, sizeof(m));
    incomingCounter = m.messageCounter;
    
    // 2. PRÜFUNG AUF RESYNC-BEFEHL
    if (m.wakeupCause == 0) {
      if (DEBUG1) Serial.printf("%s [ESP-NOW] Resync-Signal empfangen! Setze Zähler auf %u.\n", getTimeStamp().c_str(), incomingCounter);
      lastReceivedMsgCounter = incomingCounter;
      preferences.putUInt("msg_counter", lastReceivedMsgCounter); // Resync-Wert sofort speichern
      espNowLinkActive = false;
      return;
    }
  } else if (len == sizeof(FingerEvent)) {
    isFingerEvent = true;
    FingerEvent fe;
    memcpy(&fe, data, sizeof(fe));
    incomingCounter = fe.messageCounter;
  } else {
    return;
  }

  // 3. PRÜFUNG AUF REPLAY-ANGRIFF / VERALTETE NACHRICHT
  if (incomingCounter <= lastReceivedMsgCounter) {
    if (DEBUG1) Serial.printf("%s [ESP-NOW] Veraltetes Paket ignoriert (empfangen: %u, erwartet > %u).\n", getTimeStamp().c_str(), incomingCounter, lastReceivedMsgCounter);
    return;
  }

  // =========================================================================
  // Wenn wir hier ankommen, ist die Nachricht neu und gültig.
  // =========================================================================

  lastReceivedMsgCounter = incomingCounter;
  lastEspNowMessageTime = millis();

  // Nur jeden 10. Zählerstand speichern, um Flash zu schonen
  if (incomingCounter % 10 == 0) {
      preferences.putUInt("msg_counter", lastReceivedMsgCounter);
      if (DEBUG1) Serial.printf("[PREFS] Zählerstand %u in Flash gesichert.\n", lastReceivedMsgCounter);
  }

  if (!espNowLinkActive) {
    espNowLinkActive = true;
    if (DEBUG1) Serial.println(getTimeStamp() + " [ESP-NOW] Verbindung zum Sender (wieder)hergestellt.");
  }

  // 4. NACHRICHTENINHALT VERARBEITEN
  if (isSensorMessage) {
    SensorMessage m;
    memcpy(&m, data, sizeof(m));
    if (DEBUG1) Serial.printf("%s [ESP-NOW] SensorMessage #%u empfangen.\n", getTimeStamp().c_str(), m.messageCounter);

    letzteTempAussen = m.temperature;
    letzteRH_Aussen = m.humidity;
    letzteAbsAussen = m.absoluteHumidity;
    letzteTorStatus = m.torStatus;
    letzteBatteryVoltage = m.batteryVoltage;
    letzteWakeupCause = m.wakeupCause;
    receivedActionID = 0;
    fingerEventReceived = false;
    
    if (m.type == SENSOR_MESSAGE_TYPE_CONTROL_UPDATE) {
      immediateSend = true;
    }
  } else if (isFingerEvent) {
    FingerEvent fe;
    memcpy(&fe, data, sizeof(fe));
    if (DEBUG1) Serial.printf("%s [ESP-NOW] FingerEvent #%u empfangen.\n", getTimeStamp().c_str(), fe.messageCounter);

    letzteFingerID = fe.fingerID;
    letzteConfidence = fe.confidence;
    receivedActionID = fe.actionID;
    letzteTorStatus = fe.torStatus;
    letzteBatteryVoltage = fe.batteryVoltage;
    fingerEventReceived = true;
    immediateSend = true;
  }
}