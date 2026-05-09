#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <Adafruit_BME280.h>
#include "LoRa_E220.h"
#include <Ticker.h>

// =====================================================================================
//                              Globale Variablen
// =====================================================================================
// Deklariert globale Variablen und Objekte mit 'extern', damit sie
// von allen .cpp-Dateien im Projekt verwendet werden können.
// =====================================================================================

// Globale Instanzen
extern Adafruit_BME280 bme;
extern LoRa_E220 e220ttl;
extern Ticker sendTicker;

// Globale Zustandsvariablen
extern volatile bool immediateSend;
extern float letzteTempAussen, letzteRH_Aussen, letzteAbsAussen;
extern uint8_t letzteTorStatus;
extern float letzteBatteryVoltage;
extern float letzteInnenTemp, letzteInnenRH, letzteInnenAbs;
extern uint8_t letzteFingerID, letzteConfidence;
extern bool fingerEventReceived;
extern volatile uint16_t receivedActionID;

// Lüfter- und Relais-Zustände
extern bool fanState;
extern unsigned long lastSendTime;
extern uint16_t messageCounter;
extern unsigned long relayStartTime;
extern bool relayActive;

// Manueller Lüfter
extern bool manualFanActive;
extern unsigned long manualFanStartTime;
extern unsigned long lastButtonPressTime;
extern int lastButtonState;

#endif // GLOBALS_H
