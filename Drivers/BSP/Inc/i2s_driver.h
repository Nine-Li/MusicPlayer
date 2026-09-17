#ifndef __I2S_DRIVER_H
#define __I2S_DRIVER_H

#include "main.h"

void i2s2_init(void);
void i2s_test(void);
//void i2s_set_tone_freq(uint32_t freq);
char i2s_set_freq(uint32_t hz);

#endif
