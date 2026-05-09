// =====================================================================================
// GarageLuefterregler (main.cpp) - V5.9 (Maximale Sicherheit für Zähler)
// -------------------------------------------------------------------------------------
// Letzte Änderung: 5. Juli 2025 (Zähler wird bei jedem Paket persistent gespeichert)
// Funktion:        Hauptdatei, die alle Module initialisiert und die Hauptschleife
//                  ausführt.
// =====================================================================================

#include <WiFi.h>
#include <Preferences.h> // Bibliothek für persistenten Speicher
#include "config.h"
#include "structures.h"
#include "globals.h"
#include "utils.h"
#include "espnow_handler.h"
#include "lora_handler.h"
#include "fan_handler.h"

// =====================================================================================
//                              Definition der Globalen Variablen
// =====================================================================================

// Globale Instanzen
Adafruit_BME280 bme;
LoRa_E220 e220ttl(&Serial2, LORA_AUX, LORA_M0, LORA_M1);
Ticker sendTicker;
Preferences preferences; // Objekt für den Flash-Speicher

// Globale Zustandsvariablen
volatile bool immediateSend = false;
float letzteTempAussen = 0.0, letzteRH_Aussen = 0.0, letzteAbsAussen = 0.0;
uint8_t letzteTorStatus = 0;
float letzteBatteryVoltage = 0.0;
float letzteInnenTemp = NAN, letzteInnenRH = NAN, letzteInnenAbs = NAN;
uint8_t letzteFingerID = 0, letzteConfidence = 0;
bool fingerEventReceived = false;
volatile uint16_t receivedActionID = 0;
uint8_t letzteWakeupCause = 0;
uint32_t lastReceivedMsgCounter = 0;

// Watchdog-Variablen
unsigned long lastEspNowMessageTime = 0;
bool espNowLinkActive = false;

// Lüfter- und Relais-Zustände
bool fanState = false;
unsigned long lastSendTime = 0;
uint16_t messageCounter = 0;
unsigned long relayStartTime = 0;
bool relayActive = false;

// Manueller Lüfter
bool manualFanActive = false;
unsigned long manualFanStartTime = 0;
unsigned long lastButtonPressTime = 0;
int lastButtonState = LOW;


// =====================================================================================
//                                  Watchdog-Funktion
// =====================================================================================
void handle_espnow_watchdog() {
  // Prüft nur, wenn der Link als aktiv gilt
  if (espNowLinkActive && (millis() - lastEspNowMessageTime > (ESPNOW_TIMEOUT_S * 1000))) {
    if (DEBUG1) {
      Serial.println(getTimeStamp() + " [ERROR] Verbindung zu code1c verloren (Timeout)!");
      Serial.println("         > Externe Sensorwerte werden zurückgesetzt und Lüfter deaktiviert.");
    }
    
    // Status auf inaktiv setzen, um diese Meldung nur einmal auszugeben
    espNowLinkActive = false;

    // Externe Sensorwerte zurücksetzen
    letzteTempAussen = 0.0;
    letzteRH_Aussen = 0.0;
    letzteAbsAussen = 0.0;
    letzteTorStatus = 0;
    letzteBatteryVoltage = 0.0;

    // Sicherheitsabschaltung für den Lüfter
    if (fanState) {
      digitalWrite(FAN_PIN, LOW);
      fanState = false;
    }
    
    // LoRa-Meldung senden, um den Fehlerzustand zu signalisieren
    readLocalSensors();
    sendeLoraDaten(letzteInnenTemp, letzteInnenRH, letzteInnenAbs, fanState);
  }
}


// =====================================================================================
//                                      SETUP
// =====================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  if (DEBUG1) {
    Serial.println("\n" + getTimeStamp() + " [SETUP] GarageLuefterregler V5.9 (Modular) startet...");
    Serial.printf("[SETUP] Eigene MAC-Adresse: %s\n", WiFi.macAddress().c_str());
  }

  // Module initialisieren
  init_sensors();
  init_fan_relay();
  init_espnow();
  init_lora();
  
  // Preferences initialisieren und letzten Zähler aus dem Flash laden
  preferences.begin("code2-status", false); // Namespace öffnen (read/write)
  lastReceivedMsgCounter = preferences.getUInt("msg_counter", 0); // Zähler laden, Standard 0
  if (DEBUG1) Serial.printf("[PREFS] Letzter bekannter Zähler aus Flash geladen: %u\n", lastReceivedMsgCounter);
  
  // Initialisiere den Watchdog-Timer beim Start
  lastEspNowMessageTime = millis();

  // Periodischen LoRa-Sende-Ticker starten
  sendTicker.attach(SEND_INTERVAL_S, sendLoraPeriodically);

  if (DEBUG1) Serial.println(getTimeStamp() + " [SETUP] Initialisierung abgeschlossen.");
}


// =====================================================================================
//                                       LOOP
// =====================================================================================
void loop() {
  // Watchdog-Funktion bei jedem Durchlauf aufrufen
  handle_espnow_watchdog();

  // Auf eingehende LoRa-Befehle prüfen (z.B. Tor schließen)
  handle_lora_receive();

  // Prüfen, ob das Relais nach seiner Aktivierungszeit wieder ausgeschaltet werden muss
  if (relayActive && (millis() - relayStartTime >= 1000)) {
    digitalWrite(RELAY_PIN, LOW);
    relayActive = false;
    if (DEBUG1) Serial.println(getTimeStamp() + " [RELAY] Relais deaktiviert.");
  }

  // Logik für den manuellen Lüftertaster prüfen
  handle_fan_button();
  
  // Timer für den manuellen Lüftermodus überwachen
  handle_manual_fan_timer();

  // Hauptlogik, die bei einem "immediateSend"-Ereignis ausgeführt wird
  if (immediateSend) {
    immediateSend = false;

    // Relais schalten, wenn ein gültiges Finger-Event ankam UND der Link aktiv ist
    if (espNowLinkActive && fingerEventReceived && receivedActionID == GARAGE_FINGER_ACTION_ID) {
      if (DEBUG1) Serial.printf("%s [RELAY] Garagentor-Relais wird aktiviert.\n", getTimeStamp().c_str());
      digitalWrite(RELAY_PIN, HIGH);
      relayActive = true;
      relayStartTime = millis();
    }

    readLocalSensors();

    if (!isnan(letzteInnenTemp)) {
      if (!manualFanActive) {
        updateFan();
      }
      delay(100);
      sendeLoraDaten(letzteInnenTemp, letzteInnenRH, letzteInnenAbs, fanState);
    }
    receivedActionID = 0;
  }
  
  // Periodische Aktualisierung der automatischen Lüftersteuerung (falls nicht manuell aktiv)
  static unsigned long lastFanCheck = 0;
  if (espNowLinkActive && !manualFanActive && (millis() - lastFanCheck > 5000)) {
      readLocalSensors();
      updateFan();
      lastFanCheck = millis();
  }
}