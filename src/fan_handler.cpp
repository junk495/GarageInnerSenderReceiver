#include "fan_handler.h"
#include "globals.h"
#include "config.h"
#include "utils.h"

// Initialisiert die Pins für Lüfter und Relais
void init_fan_relay() {
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(FAN_BUTTON_PIN, INPUT_PULLDOWN);
}

// Automatische Lüfterlogik (mit Schmitt-Trigger / Hysterese)
void updateFan() {
  if (isnan(letzteInnenAbs) || isnan(letzteAbsAussen)) return;

  float diffAbs = letzteInnenAbs - letzteAbsAussen;
  
  // Hysterese-Wert in g/m³ (Bestimmt, wie viel trockener es werden muss, bevor abgeschaltet wird)
  const float HYSTERESE = 0.4; 
  
  // Einschalten, wenn die Feuchtigkeit drinnen deutlich höher ist als die Schwelle
  bool sollteAnSein = diffAbs > DIFFERENZ_SCHWELLE;
  
  // Ausschalten erst dann, wenn die Differenz spürbar unter die Schwelle gefallen ist
  bool sollteAusSein = diffAbs < (DIFFERENZ_SCHWELLE - HYSTERESE);

  if (!fanState && sollteAnSein && letzteTorStatus != 2) {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
      if (DEBUG1) Serial.printf("[FAN] Lüfter EIN (Diff: %.2f g/m³, Tor: %d). %s\n", diffAbs, letzteTorStatus, getTimeStamp().c_str());
  }
  else if (fanState && (sollteAusSein || letzteTorStatus == 2)) {
    digitalWrite(FAN_PIN, LOW);
    fanState = false;
    if (DEBUG1) Serial.printf("[FAN] Lüfter AUS (Diff: %.2f g/m³ oder Tor offen). %s\n", diffAbs, getTimeStamp().c_str());
  }
}

// Behandelt die Logik für den manuellen Lüftertaster (als Umschalter)
void handle_fan_button() {
  const unsigned long DEBOUNCE_DELAY = 50;
  int currentButtonState = digitalRead(FAN_BUTTON_PIN);
  if (currentButtonState != lastButtonState) {
    delay(10); // Einfaches Debouncing
    currentButtonState = digitalRead(FAN_BUTTON_PIN);
    if (currentButtonState != lastButtonState) {
      lastButtonState = currentButtonState;
      if (currentButtonState == HIGH && (millis() - lastButtonPressTime > DEBOUNCE_DELAY)) {
        
        // NEUE UMSCHALT-LOGIK
        if (!manualFanActive) {
          // Fall 1: Manuellen Modus starten
          if (DEBUG1) Serial.println(getTimeStamp() + " [BUTTON] Manueller Lüfter-Modus für " + String(MANUAL_FAN_DURATION_MINUTES) + " Minuten aktiviert.");
          manualFanActive = true;
          manualFanStartTime = millis();
          digitalWrite(FAN_PIN, HIGH);
          fanState = true;
          immediateSend = true; // LoRa-Sendung auslösen
        } else {
          // Fall 2: Manuellen Modus vorzeitig beenden
          if (DEBUG1) Serial.println(getTimeStamp() + " [BUTTON] Manueller Modus durch erneuten Tastendruck beendet. Wechsle zu Automatik.");
          manualFanActive = false;
          updateFan(); // Sofort zur Automatik zurückkehren und Lüfterstatus anpassen
          immediateSend = true; // Neuen Status per LoRa senden
        }
        lastButtonPressTime = millis();
      }
    }
  }
}

// Überwacht den Timer für den manuellen Lüfterbetrieb
void handle_manual_fan_timer() {
    if (!manualFanActive) return;

    const unsigned long MANUAL_FAN_DURATION_MS = MANUAL_FAN_DURATION_MINUTES * 60 * 1000UL;
    if (millis() - manualFanStartTime >= MANUAL_FAN_DURATION_MS) {
      if (DEBUG1) Serial.println(getTimeStamp() + " [FAN] Manuelle Lüfterzeit abgelaufen. Rückkehr zur automatischen Steuerung.");
      manualFanActive = false;
      readLocalSensors(); // Sensoren neu lesen für akkurate Automatik
      updateFan();      // Lüfterstatus sofort anpassen
    } else {
      // Sicherstellen, dass der Lüfter im manuellen Modus an bleibt
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
    }
}