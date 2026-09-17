#include "usart_driver.h"

SD_HandleTypeDef sd2_handle = {0};
DMA_HandleTypeDef sd2_dmarx = {0};
DMA_HandleTypeDef sd2_dmatx = {0};

HAL_StatusTypeDef sd2_init_status = HAL_OK;

void sd2_init(void)
{
	sd2_handle.Instance = SDIO;
	sd2_handle.Init.BusWide = SDIO_BUS_WIDE_1B;
	sd2_handle.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
	sd2_handle.Init.ClockDiv = SDIO_TRANSFER_CLK_DIV;
	sd2_handle.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
	sd2_handle.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
	sd2_handle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_ENABLE;
	
	if(HAL_SD_Init(&sd2_handle))
	{
		Error_Handler();
	}

	if(HAL_SD_ConfigWideBusOperation(&sd2_handle, SDIO_BUS_WIDE_4B) != HAL_OK)
	{
		Error_Handler();
	}

	__HAL_SD_CLEAR_FLAG(&sd2_handle, SDIO_STATIC_FLAGS);
	NVIC_ClearPendingIRQ(SDIO_IRQn);
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
	__HAL_RCC_SDIO_CLK_ENABLE();
	/*gpio已在gpio.c中初始化*/
	HAL_NVIC_SetPriority(SDIO_IRQn, 14, 0);
	HAL_NVIC_EnableIRQ(SDIO_IRQn);

	sd2_dmarx.Instance = DMA2_Stream3;
	sd2_dmarx.Init.Channel = DMA_CHANNEL_4;
	sd2_dmarx.Init.Direction = DMA_PERIPH_TO_MEMORY;
	sd2_dmarx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
	sd2_dmarx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
	sd2_dmarx.Init.MemBurst = DMA_MBURST_SINGLE;
	sd2_dmarx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	sd2_dmarx.Init.MemInc = DMA_MINC_ENABLE;
	sd2_dmarx.Init.Mode = DMA_PFCTRL;
	sd2_dmarx.Init.PeriphBurst = DMA_PBURST_INC4;
	sd2_dmarx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	sd2_dmarx.Init.PeriphInc = DMA_PINC_DISABLE;
	sd2_dmarx.Init.Priority = DMA_PRIORITY_MEDIUM;

	if(HAL_DMA_Init(&sd2_dmarx))
	{
		Error_Handler();
	}

	sd2_dmatx.Instance = DMA2_Stream6;
	sd2_dmatx.Init.Channel = DMA_CHANNEL_4;
	sd2_dmatx.Init.Direction = DMA_MEMORY_TO_PERIPH;
	sd2_dmatx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
	sd2_dmatx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
	sd2_dmatx.Init.MemBurst = DMA_MBURST_SINGLE;
	sd2_dmatx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	sd2_dmatx.Init.MemInc = DMA_MINC_ENABLE;
	sd2_dmatx.Init.Mode = DMA_PFCTRL;
	sd2_dmatx.Init.PeriphBurst = DMA_PBURST_INC4;
	sd2_dmatx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	sd2_dmatx.Init.PeriphInc = DMA_PINC_DISABLE;
	sd2_dmatx.Init.Priority = DMA_PRIORITY_MEDIUM;

	if(HAL_DMA_Init(&sd2_dmatx))
	{
		Error_Handler();
	}

	__HAL_LINKDMA(hsd, hdmarx, sd2_dmarx);
	__HAL_LINKDMA(hsd, hdmatx, sd2_dmatx);

	HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 14, 14);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
	__HAL_RCC_SDIO_CLK_DISABLE();
	HAL_NVIC_DisableIRQ(SDIO_IRQn);

	HAL_DMA_DeInit(hsd->hdmarx);
	HAL_DMA_DeInit(hsd->hdmatx);
}
