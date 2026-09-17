#ifndef __QUEUESTUDY_H
#define __QUEUESTUDY_H

#include "freeRTOS.h"

typedef struct
{
  uint8_t cSendData;
  TickType_t xPeriodDelay;
}xTaskParameters;

void vTxTask( void *pvParameters );
void vRxTask( void *pvParameters );

#endif
