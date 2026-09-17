#ifndef __USART_H
#define __USART_H

#include "main.h"

void vUsart1_Init( void );
void vUsart1_IRQHandler( uint8_t isIDLE );
int32_t iUsart1_Printf( const char *format, ... );
uint8_t ucUsartReceiveData( char *pcDstBuffer, uint32_t uiSize );
uint8_t Usart1_Return_Rx_FirstChar(void);
uint32_t Usart1_Return_Rx_Avail(void);
void vUsart1_Tx_GateKeeper(void *vpParams);

#endif
