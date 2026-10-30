#ifndef APP_H
#define APP_H

#include <stdint.h>

void App_Init(void);
void App_Loop(void);
void App_USB_Receive(uint8_t *buf, uint32_t len);

#endif