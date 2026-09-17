#ifndef __MMF_H
#define __MMF_H

#include "main.h"

uint8_t mmf_fix_table(void);
uint8_t mmf_init(void);
void *mmf_malloc(size_t size);
uint8_t mmf_free(void *addr);
uint8_t mmf_test(void);

extern UART_HandleTypeDef husart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

#endif
