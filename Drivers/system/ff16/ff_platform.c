#include "main.h"
#include "ff_platform.h"
#include "sd2_driver.h"
#include "usart_driver.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#define SD_WRITE_TIMEOUT 200u

typedef enum
{
  SDRead,
  SDWrite
}SD_Dir_t;

typedef struct 
{
  SD_Dir_t xSD_Dir;
  SD_HandleTypeDef *hsd;
  uint8_t *psDst;
  const uint8_t *psSrc;
  uint32_t ulBlockAdd;
  uint32_t ulNumberOfBlocks;
  int result;
  SemaphoreHandle_t xDone;
}xSDReq_t;

SemaphoreHandle_t xSDRead_DMADoneSemaphr = 0;
QueueHandle_t xSDQueue = {0};

/* 常驻对齐 bounce: 单 gatekeeper 串行访问, 读写共用; 避免按请求从堆申请 count*512 */
static uint8_t xSDBounce[SD_BLOCKSIZE] __attribute__((aligned(4)));

int FLASH_disk_status(void)
{
  return 0;
}  
int SD_disk_status(void)
{
  return HAL_SD_GetCardState(&sd2_handle);
}

int USB_disk_status(void)
{
  return 0;
}

int FLASH_disk_initialize(void)
{
  return 0;
}
int SD_disk_initialize(void)
{
  static uint8_t ucSDInited = 0;

  if ( !ucSDInited )
  {
    if (xSDQueue == 0)
    {
      xSDQueue = xQueueCreate( 10, sizeof( xSDReq_t * ));
      if ( !xSDQueue ) 
        return 1;
    }

    if (xSDRead_DMADoneSemaphr == 0)
    {
      xSDRead_DMADoneSemaphr = xSemaphoreCreateBinary(  );
      if ( !xSDRead_DMADoneSemaphr ) 
        return 1;
    }
    
    sd2_init();
    ucSDInited = 1;
  }

  return 0;
}
int USB_disk_initialize(void)
{
  return 0;
}

int FLASH_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
  UNUSED(buff);
  UNUSED(sector);
  UNUSED(count);
  return 0;
}

int SD_disk_read(BYTE *buff, LBA_t sector, UINT count)
{	
  xSDReq_t *req;
  int result;

  if ((buff == NULL) || (count == 0))
  {
    return 1;
  }

  req = (xSDReq_t *)pvPortMalloc( sizeof( xSDReq_t ) );
  if (req == NULL)
  {
    return 1;
  }

  req->xDone = xSemaphoreCreateBinary(  );
  if (req->xDone == NULL)
  {
    vPortFree(req);
    return 1;
  }

  req->xSD_Dir = SDRead;
  req->hsd = &sd2_handle;
  req->psDst = buff;          /* 直接指向调用方缓冲, gatekeeper 逐块拷入 */
  req->psSrc = NULL;
  req->ulBlockAdd = sector;
  req->ulNumberOfBlocks = count;
  req->result = 1;

  if (xQueueSend( xSDQueue, (void *)&req, pdMS_TO_TICKS( 200 ) ) != pdPASS)
  {
    vSemaphoreDelete(req->xDone);
    vPortFree(req);
    return 1;
  }

  xSemaphoreTake(req->xDone, portMAX_DELAY);

  result = req->result;

  vSemaphoreDelete(req->xDone);
  vPortFree(req);
  return result;
}

int USB_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
  UNUSED(buff);
  UNUSED(sector);
  UNUSED(count);
  return 0;
}

int FLASH_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
  UNUSED(buff);
  UNUSED(sector);
  UNUSED(count);
  return 0;
}
int SD_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
  xSDReq_t *req;
  int result;

  if ((buff == NULL) || (count == 0))
  {
    return -1;
  }

  req = (xSDReq_t *)pvPortMalloc( sizeof( xSDReq_t ) );
  if (req == NULL)
  {
    return 1;
  }

  req->xDone = xSemaphoreCreateBinary(  );
  if (req->xDone == NULL)
  {
    vPortFree(req);
    return 1;
  }

  req->xSD_Dir = SDWrite;
  req->hsd = &sd2_handle;
  req->psDst = NULL;
  req->psSrc = buff;
  req->ulBlockAdd = sector;
  req->ulNumberOfBlocks = count;
  req->result = 1;

  if (xQueueSend( xSDQueue, (void *)&req, pdMS_TO_TICKS( 200 ) ) != pdPASS)
  {
    vSemaphoreDelete(req->xDone);
    vPortFree(req);
    return 1;
  }

  xSemaphoreTake(req->xDone, portMAX_DELAY);

  result = req->result;

  vSemaphoreDelete(req->xDone);
  vPortFree(req);
  return result;
}
int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
  UNUSED(buff);
  UNUSED(sector);
  UNUSED(count);
  return 0;
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if(hsd->Instance == SDIO)
  {
    if (xSDRead_DMADoneSemaphr)
      xSemaphoreGiveFromISR( xSDRead_DMADoneSemaphr, &xHigherPriorityTaskWoken);
  }
  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void vSD_GateKeeper( void *param)
{
  xSDReq_t *req = NULL;

  TickType_t xWaitDelay = pdMS_TO_TICKS(200);

  while (1)
  {
    if (xQueueReceive( xSDQueue, &req, portMAX_DELAY ) != pdPASS)
    {
      continue;
    }

    if (req->xSD_Dir == SDRead)
    {
      UINT i;

      /* 逐块 DMA 进常驻 bounce 再拷入调用方: 不按 count 申请堆, 也不受簇大小影响 */
      req->result = 0;
      for (i = 0; i < req->ulNumberOfBlocks; i++)
      {
        xSemaphoreTake( xSDRead_DMADoneSemaphr, 0 );

        if (HAL_SD_ReadBlocks_DMA( req->hsd, xSDBounce, (uint32_t)(req->ulBlockAdd + i), 1 ) != HAL_OK)
        {
          req->result = 1;
          break;
        }

        if (xSemaphoreTake( xSDRead_DMADoneSemaphr, xWaitDelay ) == pdFALSE)
        {
          HAL_SD_Abort( &sd2_handle );
          req->result = 1;
          break;
        }

        memcpy( req->psDst + (i * SD_BLOCKSIZE), xSDBounce, SD_BLOCKSIZE );
      }

      xSemaphoreGive( req->xDone );
    }

    if (req->xSD_Dir == SDWrite)
    {
      UINT i;

      //写路径不复用DMA双缓冲：逐块拷入常驻对齐scratch后轮询写入
      req->result = 0;
      for (i = 0; i < req->ulNumberOfBlocks; i++)
      {
        memcpy( xSDBounce, req->psSrc + (i * SD_BLOCKSIZE), SD_BLOCKSIZE );

        if (HAL_SD_WriteBlocks( req->hsd, xSDBounce, (uint32_t)(req->ulBlockAdd + i), 1, SD_WRITE_TIMEOUT) != HAL_OK )
        {
          req->result = 1;
          break;
        }
      }

      xSemaphoreGive( req->xDone );
    }
  }
}

