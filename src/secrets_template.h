#ifndef SECRETS_H
#define SECRETS_H
#include <stdint.h>

// Geheimer Schlüssel für ESP-NOW (Primary Master Key). MUSS exakt 16 Zeichen lang sein!
static const char* pmk_key_str = "1234567890123456"; 

// MAC-Adresse deines Senders (code1c).
// Nur Nachrichten von dieser Adresse werden vom Empfänger akzeptiert.
static const uint8_t known_sender_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 

#endif // SECRETS_H