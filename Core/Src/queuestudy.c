#include "main.h"
#include "queuestudy.h"
#include "queue.h"
#include "task.h"

extern QueueHandle_t xQueue1;

void vTxTask( void *pvParameters )
{
  xTaskParameters *pxParams = (xTaskParameters *)pvParameters;

  uint8_t cTask1Send = pxParams->cSendData;

  TickType_t xTxTimeOut = pdMS_TO_TICKS( 100 );
  TickType_t xPeriodDelay = pdMS_TO_TICKS( pxParams->xPeriodDelay );
  TickType_t xPreviousWakeTime = xTaskGetTickCount(  );

  while( 1 )
  {
    xQueueSendToBack( xQueue1, &cTask1Send, xTxTimeOut );
    xTaskDelayUntil( &xPreviousWakeTime, xPeriodDelay );
  }
}

void vRxTask( void *pvParameters )
{
  uint8_t cRxData = 0;

  while( 1 )
  {
    if( xQueueReceive( xQueue1, &cRxData, portMAX_DELAY ) == pdPASS )
    {
      switch ( cRxData )
      {
      case 0xAA:
        HAL_GPIO_TogglePin( GPIOF, GPIO_PIN_9 );
        break;

      case 0x55:
        HAL_GPIO_TogglePin( GPIOF, GPIO_PIN_10 );
        break;

      default:
        break;
      }
    }
  }
}
