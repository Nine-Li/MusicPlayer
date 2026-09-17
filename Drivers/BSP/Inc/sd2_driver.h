#ifndef __SD2_H
#define __SD2_H

#include "main.h"

void sd2_init(void);

extern SD_HandleTypeDef sd2_handle;
extern DMA_HandleTypeDef sd2_dmarx;

#endif 
