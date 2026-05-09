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

// Automatische Lüfterlogik
void updateFan() {
  if (isnan(letzteInnenAbs) || isnan(letzteAbsAussen)) return;

  float diffAbs = letzteInnenAbs - letzteAbsAussen;
  bool feuchteBedingungErfuellt = diffAbs > DIFFERENZ_SCHWELLE;

  if (!fanState && feuchteBedingungErfuellt && letzteTorStatus != 2) {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
      if (DEBUG1) Serial.printf("[FAN] Lüfter eingeschaltet (Torstatus: %d) %s\n", letzteTorStatus, getTimeStamp().c_str());
  }
  else if (fanState && (!feuchteBedingungErfuellt || letzteTorStatus == 2)) {
    digitalWrite(FAN_PIN, LOW);
    fanState = false;
    if (DEBUG1) Serial.printf("[FAN] Lüfter ausgeschaltet (Bedingung nicht erfüllt ODER Torstatus: %d). %s\n", letzteTorStatus, getTimeStamp().c_str());
  }
}

// Behandelt die Logik für den manuellen Lüftertaster
void handle_fan_button() {
  const unsigned long DEBOUNCE_DELAY = 50;
  int currentButtonState = digitalRead(FAN_BUTTON_PIN);
  if (currentButtonState != lastButtonState) {
    delay(10);
    currentButtonState = digitalRead(FAN_BUTTON_PIN);
    if (currentButtonState != lastButtonState) {
      lastButtonState = currentButtonState;
      if (currentButtonState == HIGH && (millis() - lastButtonPressTime > DEBOUNCE_DELAY)) {
        if (DEBUG1) Serial.println(getTimeStamp() + " [BUTTON] Lüfter-Taster gedrückt. Starte manuellen Modus.");
        manualFanActive = true;
        manualFanStartTime = millis();
        digitalWrite(FAN_PIN, HIGH);
        fanState = true;
        lastButtonPressTime = millis();
      }
    }
  }
}

// Überwacht den Timer für den manuellen Lüfterbetrieb
void handle_manual_fan_timer() {
    if (!manualFanActive) return;

    const unsigned long MANUAL_FAN_DURATION_MS = MANUAL_FAN_DURATION_MINUTES * 60 * 1000;
    if (millis() - manualFanStartTime >= MANUAL_FAN_DURATION_MS) {
      if (DEBUG1) Serial.println(getTimeStamp() + " [FAN] Manuelle Lüfterzeit abgelaufen. Rückkehr zur automatischen Steuerung.");
      manualFanActive = false;
      readLocalSensors(); // Sensoren neu lesen für akkurate Automatik
      updateFan();      // Lüfterstatus sofort anpassen
    } else {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
    }
}
