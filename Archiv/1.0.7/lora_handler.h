#ifndef LORA_HANDLER_H
#define LORA_HANDLER_H

void init_lora();
void sendLoraPeriodically();
void handle_lora_receive();
void sendeLoraDaten(float tIn, float rhIn, float absIn, bool fan);

#endif // LORA_HANDLER_H
