#ifndef SECRETS_H
#define SECRETS_H

// =====================================================================================
//                          GEHEIMER ESP-NOW SCHLÜSSEL
// =====================================================================================
// Diese Datei enthält den geheimen Schlüssel für die verschlüsselte
// Kommunikation zwischen dem Garagen-Sensor (code1c) und dem
// Garagen-Controller (code2).
// =====================================================================================


// -------------------- ESP-NOW (benötigt von code1c & code2) --------------------
// Geheimer Schlüssel für die ESP-NOW Verschlüsselung (Primary Master Key).
// WICHTIG: Muss exakt 16 Zeichen lang sein.
// KORREKTUR: 'static const' wird verwendet, um die Variable korrekt in
// einer Header-Datei zu definieren und Linker-Fehler zu vermeiden.
static const char* pmk_key_str = "YourSecretKey"; // <-- Hier den eigenen geheimen Schlüssel eintragen (16 Zeichen) 


#endif // SECRETS_H