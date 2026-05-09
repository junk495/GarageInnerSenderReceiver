#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>

void init_espnow();
void onReceive(const uint8_t *mac, const uint8_t *data, int len);

#endif // ESPNOW_HANDLER_H
