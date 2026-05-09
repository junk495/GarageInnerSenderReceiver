#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <Wire.h>
#include "globals.h"
#include "config.h"

// =====================================================================================
//                              Hilfsfunktionen
// =====================================================================================
// KORREKTUR: Die Reihenfolge der Funktionen wurde geändert, um Kompilierungsfehler
// zu beheben. 'getTimeStamp' muss vor der ersten Verwendung deklariert sein.
// =====================================================================================


// Gibt einen formatierten Zeitstempel zurück
inline String getTimeStamp() {
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

// Initialisiert Sensoren
inline void init_sensors() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (!bme.begin(0x76)) {
        if (DEBUG1) Serial.println(getTimeStamp() + " [ERROR] BME280 nicht gefunden!");
        while (1);
    }
}

// Berechnet die absolute Feuchtigkeit
inline float calculateAbsoluteHumidity(float temperature, float humidity) {
  return (6.112 * pow(2.71828, (17.67 * temperature) / (temperature + 243.5)) * humidity * 2.1674) / (273.15 + temperature);
}

// Liest die lokalen Sensordaten und wendet Korrekturen an
inline void readLocalSensors() {
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

  t += BME_INNER_TEMP_OFFSET_C;
  rh += BME_INNER_HUM_OFFSET_PERCENT;

  letzteInnenTemp = t;
  letzteInnenRH = rh;
  letzteInnenAbs = calculateAbsoluteHumidity(t, rh);
}


#endif // UTILS_H
