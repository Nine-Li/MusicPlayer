/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
#include "freeRTOS.h"
#include "queue.h"
#include "task.h"
#include "list.h"
#include "semphr.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern SemaphoreHandle_t xUsart1_Rx_Semaphore;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
extern void xPortSysTickHandler( void );
extern void vUsart1_IRQHandler( uint8_t isIDLE );
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  HAL_RCC_NMI_IRQHandler();
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  #if (INCLUDE_xTaskGetSchedulerState  == 1 )
  if (xTaskGetSchedulerState(  ) != taskSCHEDULER_NOT_STARTED)
  {
  #endif  /* INCLUDE_xTaskGetSchedulerState */  
    xPortSysTickHandler(  );
  #if (INCLUDE_xTaskGetSchedulerState  == 1 )
  }
  #endif  /* INCLUDE_xTaskGetSchedulerState */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick(  );
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */
/*
 * SVC_Handler / PendSV_Handler are intentionally NOT defined here.
 * FreeRTOSConfig.h maps vPortSVCHandler -> SVC_Handler and
 * xPortPendSVHandler -> PendSV_Handler (direct routing), so both handlers
 * are defined in port.c. Re-adding them here causes duplicate symbols.
 */

void USART1_IRQHandler( void )
{
  BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

  if ( __HAL_USART_GET_FLAG( &husart1, USART_FLAG_IDLE ) )
  {
    __HAL_USART_CLEAR_IDLEFLAG( &husart1 );
    vUsart1_IRQHandler( 1 ); //更新 uiRxTotal变量
    xSemaphoreGiveFromISR(xUsart1_Rx_Semaphore, &pxHigherPriorityTaskWoken);
  }

  HAL_UART_IRQHandler( &husart1 );

  portYIELD_FROM_ISR( pxHigherPriorityTaskWoken );
}

void DMA2_Stream2_IRQHandler( void )
{
  HAL_DMA_IRQHandler( &hdma_usart1_rx ); 
}

void DMA2_Stream7_IRQHandler( void )
{
  HAL_DMA_IRQHandler( &hdma_usart1_tx ); 
}

void SDIO_IRQHandler( void )
{
  HAL_SD_IRQHandler( &sd2_handle );
}

void DMA2_Stream3_IRQHandler( void )
{
  HAL_DMA_IRQHandler( &sd2_dmarx ); 
}

void DMA2_Stream6_IRQHandler( void )
{
  HAL_DMA_IRQHandler( &sd2_dmatx ); 
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2s_dmatx); 
}
void DMA1_Stream3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2s_dmarx); 
}
void SPI2_IRQHandler(void)
{ 
  HAL_I2S_IRQHandler(&i2s2_handle);
}
/* USER CODE END 1 */
