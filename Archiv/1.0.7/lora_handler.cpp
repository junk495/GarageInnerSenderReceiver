#include "lora_handler.h"
#include "globals.h"
#include "structures.h"
#include "config.h"
#include "utils.h"
#include "fan_handler.h" // KORREKTUR: Fehlendes Include für die Lüfter-Funktionen hinzugefügt

// Initialisiert das LoRa-Modul
void init_lora() {
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
    if (DEBUG1) Serial.printf("%s [LoRa] Modul konfiguriert, Status: %s\n", getTimeStamp().c_str(), status.getResponseDescription().c_str());
    cfgContainer.close();
  } else {
    if (DEBUG1) Serial.printf("%s [LoRa] Konfig lesen fehlgeschlagen: %s\n", getTimeStamp().c_str(), cfgContainer.status.getResponseDescription().c_str());
    cfgContainer.close();
    while (1);
  }
}

// Sendet die gesammelten Daten via LoRa
void sendeLoraDaten(float tIn, float rhIn, float absIn, bool fan) {
  if (millis() - lastSendTime < 2000) {
    if (DEBUG2) Serial.println("[LORA] Senden übersprungen: Zu kurz nach letztem." + getTimeStamp());
    return;
  }

  unsigned long startWait = millis();
  while (digitalRead(LORA_AUX) == LOW) {
    if (millis() - startWait > 1000) {
      if (DEBUG2) Serial.println("[ERROR] LoRa-Modul ist dauerhaft beschäftigt!" + getTimeStamp());
      return;
    }
    delay(1);
  }

  LoRaPayload p{};
  p.messageID = messageCounter++;
  p.temperatureInnen = tIn;
  p.humidityInnen = rhIn;
  p.absHumidityInnen = absIn;
  p.temperatureAussen = letzteTempAussen;
  p.humidityAussen = letzteRH_Aussen;
  p.absHumidityAussen = letzteAbsAussen;
  p.fanOn = fan;
  p.torStatus = letzteTorStatus;
  p.fingerEventValid = fingerEventReceived;
  p.fingerID = letzteFingerID;
  p.confidence = letzteConfidence;
  p.actionID = receivedActionID;
  p.batteryVoltage = letzteBatteryVoltage;

  ResponseStatus rs = e220ttl.sendFixedMessage(0, 2, 23, &p, sizeof(p));
  
  // NEU: Detaillierte Monitorausgabe für gesendete LoRa-Daten wiederhergestellt
  if (DEBUG2) {
    if (rs.code == E220_SUCCESS) {
      Serial.printf("[LORA] Sende Daten... %s\n", getTimeStamp().c_str());
      Serial.printf("  > MessageID: %u\n", p.messageID);
      Serial.printf("  > Innen: T=%.1f, RH=%.1f, Abs=%.1f\n", p.temperatureInnen, p.humidityInnen, p.absHumidityInnen);
      Serial.printf("  > Aussen: T=%.1f, RH=%.1f, Abs=%.1f\n", p.temperatureAussen, p.humidityAussen, p.absHumidityAussen);
      Serial.printf("  > Status: Fan=%d, Tor=%d, Vbat=%.2f V\n", p.fanOn ? 1 : 0, p.torStatus, p.batteryVoltage);
      if (p.fingerEventValid) {
        Serial.printf("  > Finger-Event: ID=%d, Conf=%d, ActionID=%d\n", p.fingerID, p.confidence, p.actionID);
      }
    } else {
      Serial.printf("[ERROR] LoRa Sendefehler: %s %s\n", rs.getResponseDescription().c_str(), getTimeStamp().c_str());
    }
  }
  
  lastSendTime = millis();
  fingerEventReceived = false;
}

// Wird vom Ticker aufgerufen, um periodisch zu senden
void sendLoraPeriodically() {
  readLocalSensors();
  if (isnan(letzteInnenTemp) || isnan(letzteInnenRH)) {
    return;
  }
  if (!manualFanActive) {
      updateFan();
  }
  sendeLoraDaten(letzteInnenTemp, letzteInnenRH, letzteInnenAbs, fanState);
}

// Prüft auf eingehende LoRa-Befehle
void handle_lora_receive() {
  if (e220ttl.available() >= sizeof(CommandPayload)) {
    ResponseStructContainer rsc = e220ttl.receiveMessage(sizeof(CommandPayload));
    if (rsc.status.code == E220_SUCCESS) {
      CommandPayload cmd;
      memcpy(&cmd, rsc.data, sizeof(CommandPayload));

      if (cmd.command == 1) { // 1 = TOGGLE_RELAY
        if (DEBUG1) Serial.println(getTimeStamp() + " [RELAY] Relais-Ansteuerung via LoRa-Befehl empfangen. Prüfe lokalen Torstatus...");
        
        if (letzteTorStatus == 1 || letzteTorStatus == 2) {
            if (DEBUG1) Serial.println(getTimeStamp() + " [RELAY] Lokaler Status ist 'offen'. Aktiviere Relais.");
            digitalWrite(RELAY_PIN, HIGH);
            relayActive = true;
            relayStartTime = millis();
        } else {
            if (DEBUG1) Serial.printf("%s [RELAY] Befehl ignoriert. Lokaler Torstatus ist bereits %d (geschlossen).\n", getTimeStamp().c_str(), letzteTorStatus);
        }
      }
    }
    rsc.close();
  }
}
