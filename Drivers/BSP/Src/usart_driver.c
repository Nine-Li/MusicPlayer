#include "usart_driver.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USE_FREERTOS 1

/*FreeRTOS相关*/
#if(USE_FREERTOS)
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

/* TX 队列项自带数据副本(整项入队), 不传调用方栈指针, 避免悬垂 */
#define TX_MSG_MAX 384

typedef struct
{
    uint8_t Data[TX_MSG_MAX];
    uint16_t Len;
}Usart_TxQueue_t;

QueueHandle_t xUsaer1_Tx_Queue = {0};
extern SemaphoreHandle_t xUsart1_Rx_Semaphore;

#endif
/*FreeRTOS相关*/

/* HAL_UART(异步全双工)模块驱动: gState/RxState 分离, 循环 RX 与并发 TX 互不阻塞。
   (曾用 HAL_USART 同步模块: 其单 State 字段 + Receive_DMA 附带启动 TX DMA 的设计
   与"循环RX + 独立TX"不兼容, 见 AGENTS.md。勿改回。) */
UART_HandleTypeDef husart1 = {0};
DMA_HandleTypeDef hdma_usart1_rx = {0};
DMA_HandleTypeDef hdma_usart1_tx = {0};

/* ===== 扁平紧凑 TX 队列（DMA 始终从 xTxRing[0] 发送，数据恒连续无换行） ===== */
#define TX_RING_SIZE       4096
#define RX_RING_SIZE       4096
#define TX_BLOCK_ON_FULL   1        /* 1=满则阻塞等待(零丢失) 0=满则丢弃 */
#define TX_BLOCK_TIMEOUT_MS 100     /* 阻塞超时兜底, 防 TX DMA 故障死锁; 0=无限等 */

static uint8_t xTxRing[TX_RING_SIZE] __attribute__((aligned(4)));
static volatile uint16_t usTxCount = 0;    /* 待发字节数（含正在 DMA 的） */
static volatile uint16_t usTxDmaLen = 0;  /* 当前 DMA 段长度 */
static volatile uint8_t  ucTxBusy = 0;

static uint8_t xRxRing[RX_RING_SIZE] __attribute__((aligned(4)));
/* 接收采用"累计字节"模型, 且每个事件(半/满/IDLE)都用 (整环计数, NDTR) 重新快照,
   无累计误差; 消费侧按 uiRxConsumed 追读, 二者之差为可读字节数。
   任意轮询频率都不会重复取数; 消费落后一整圈以上时丢弃最旧并报丢失。 */
static volatile uint32_t uiRxTotal = 0;      /* ISR 写入: 累计已接收字节数 */
static uint32_t uiRxConsumed = 0;            /* 消费侧: 累计已读取字节数 */
static volatile uint32_t uiRxBlockCnt = 0;   /* ISR 写入: 已完成的整环(4096B)数 */

/* 依据 (整环计数, DMA NDTR) 快照当前绝对已收字节数。
   NDTR 刚重载/恰好为 0 时按整环边界处理, 避免虚报一整圈。 */
static uint32_t ucUsartRxSnap( void )
{
    uint16_t usPos = ( uint16_t )( RX_RING_SIZE - ( uint16_t )__HAL_DMA_GET_COUNTER( &hdma_usart1_rx ) );

    if ( usPos == RX_RING_SIZE )
    {
        usPos = 0;
    }
    return uiRxBlockCnt * RX_RING_SIZE + usPos;
}

static void vUsart_Tx_Start( void )
{
    uint16_t n = usTxCount;
    if (n == 0) return;
    ucTxBusy = 1;
    usTxDmaLen = n;                       /* 整段一次 DMA */
    HAL_UART_Transmit_DMA(&husart1, xTxRing, n);
}

/**
 * @brief 初始化串口
 * @param void
 * @return void
 */
void vUsart1_Init( void )
{
    husart1.Instance = USART1;
    husart1.Init.BaudRate = 115200;
    husart1.Init.Mode = UART_MODE_TX_RX;
    husart1.Init.Parity = UART_PARITY_NONE;
    husart1.Init.StopBits = UART_STOPBITS_1;
    husart1.Init.WordLength = UART_WORDLENGTH_8B;
    husart1.Init.OverSampling = UART_OVERSAMPLING_16;
    husart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&husart1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 计数与 DMA 环起点对齐(重启/错误恢复后也从此一致) */
    uiRxTotal = 0;
    uiRxConsumed = 0;
    uiRxBlockCnt = 0;

#if(USE_FREERTOS)

    xUsaer1_Tx_Queue = xQueueCreate( 10, sizeof(Usart_TxQueue_t));
		
		if ( !xUsaer1_Tx_Queue )
		{
			HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
			while (1);
		}

#endif

    /* 使能 IDLE 空闲中断: 短命令行在帧尾即可被精确结算, 无需等半/满块事件 */
    __HAL_UART_ENABLE_IT(&husart1, UART_IT_IDLE);
    HAL_UART_Receive_DMA( &husart1, xRxRing, RX_RING_SIZE);
}

void HAL_UART_MspInit( UART_HandleTypeDef *huart )
{
    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();

        /*GPIOA时钟已在gpio.c中开启*/
        GPIO_InitTypeDef gpio_initstructure = {0};
        gpio_initstructure.Alternate = GPIO_AF7_USART1;
        gpio_initstructure.Mode = GPIO_MODE_AF_PP;
        gpio_initstructure.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio_initstructure.Pull = GPIO_NOPULL;
        gpio_initstructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio_initstructure);

        HAL_NVIC_SetPriority(USART1_IRQn, 15, 15);
        HAL_NVIC_EnableIRQ(USART1_IRQn);

        __HAL_RCC_DMA2_CLK_ENABLE();

        hdma_usart1_rx.Instance = DMA2_Stream2;
        hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        hdma_usart1_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        hdma_usart1_rx.Init.MemBurst = DMA_MBURST_SINGLE;
        hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
        hdma_usart1_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;

        if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
        {
            Error_Handler();
        }

        hdma_usart1_tx.Instance = DMA2_Stream7;
        hdma_usart1_tx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        hdma_usart1_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        hdma_usart1_tx.Init.MemBurst = DMA_MBURST_SINGLE;
        hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart1_tx.Init.Mode = DMA_NORMAL;
        hdma_usart1_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;

        if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_LINKDMA(huart, hdmarx, hdma_usart1_rx);
        __HAL_LINKDMA(huart, hdmatx, hdma_usart1_tx);

        /* 调用 FreeRTOS FromISR API 的中断必须不高于 0xF0 上限: 与 USART1 同置 15 */
        HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 15, 15);
        HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

        HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 15, 15);
        HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    }
}

void HAL_UART_MspDeInit( UART_HandleTypeDef *huart )
{
    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_DISABLE();

        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);

        HAL_DMA_DeInit(huart->hdmarx);
        HAL_DMA_DeInit(huart->hdmatx);

        HAL_NVIC_DisableIRQ(USART1_IRQn);
    }
}

/**
 * @brief 向串口发送数据
 * @param format 待打印字符串
 * @return 返回 >=0 表示字节长度，-1 表示错误或超时丢弃
 * @note 不得在中断上下文调用（阻塞等待依赖线程态 + TX 完成中断排空）
 */
int32_t iUsart1_Printf( const char *format, ... )
{
    char buf[256];
    va_list args;
    int len;

    va_start(args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len < 0) return -1;
    if (len > (int)sizeof(buf)) len = (int)sizeof(buf);
    if (len == 0) return 0;

#if(!USE_FREERTOS)

    __disable_irq();
#if TX_BLOCK_ON_FULL
    if (TX_BLOCK_TIMEOUT_MS > 0)
    {
        uint32_t t0 = HAL_GetTick();
        while (usTxCount + len > TX_RING_SIZE)
        {
            __enable_irq();               /* 放开中断让 TX 完成回调排空 */
            if (HAL_GetTick() - t0 > TX_BLOCK_TIMEOUT_MS)
            {
                __disable_irq();
                goto drop;
            }
            __disable_irq();
        }
    }
    else
    {
        while (usTxCount + len > TX_RING_SIZE) { __enable_irq(); __disable_irq(); }
    }
#else
    if (usTxCount + len > TX_RING_SIZE) goto drop;
#endif

    memcpy(xTxRing + usTxCount, buf, (size_t)len);  /* 尾部连续追加 */
    usTxCount += len;

    if (!ucTxBusy)
    {
        ucTxBusy = 1;
        usTxDmaLen = usTxCount;
        __enable_irq();
            HAL_UART_Transmit_DMA(&husart1, xTxRing, usTxDmaLen);  /* 中断外启动 */
            return len;
    }
    __enable_irq();
    return len;

drop:
    __enable_irq();
    return -1;

#else

    Usart_TxQueue_t Usart_TxQueue = {0};

    if (len > TX_MSG_MAX) len = TX_MSG_MAX;
    memcpy(Usart_TxQueue.Data, buf, (size_t)len);
    Usart_TxQueue.Len = (uint16_t)len;
    xQueueSend( xUsaer1_Tx_Queue, &Usart_TxQueue, portMAX_DELAY );
    return len;

#endif

}

#if(USE_FREERTOS)

void vUsart1_Tx_GateKeeper(void *vpParams)
{
    Usart_TxQueue_t Usart_TxQueue = {0};

    static UBaseType_t uxHighWaterMark = 0;
		(void)uxHighWaterMark;

    (void)vpParams;

    while (1)
    {
        xQueueReceive( xUsaer1_Tx_Queue, &Usart_TxQueue, portMAX_DELAY);

        __disable_irq();
#if TX_BLOCK_ON_FULL
        if (TX_BLOCK_TIMEOUT_MS > 0)
        {
            uint32_t t0 = HAL_GetTick();
            while (usTxCount + Usart_TxQueue.Len > TX_RING_SIZE)
            {
                __enable_irq();               /* 放开中断让 TX 完成回调排空 */
                if (HAL_GetTick() - t0 > TX_BLOCK_TIMEOUT_MS)
                {
                    __disable_irq();
                    goto drop;
                }
                __disable_irq();
            }
        }
        else
        {
            while (usTxCount + Usart_TxQueue.Len > TX_RING_SIZE) { __enable_irq(); __disable_irq(); }
        }
#else
        if (usTxCount + Usart_TxQueue.Len > TX_RING_SIZE) goto drop;
#endif

        memcpy(xTxRing + usTxCount, Usart_TxQueue.Data, (size_t)Usart_TxQueue.Len);  /* 尾部连续追加 */
        usTxCount += Usart_TxQueue.Len;

        if (!ucTxBusy)
        {
            ucTxBusy = 1;
            usTxDmaLen = usTxCount;
            __enable_irq();
            HAL_UART_Transmit_DMA(&husart1, xTxRing, usTxDmaLen);  /* 中断外启动 */
        }
drop:
        __enable_irq();

        uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    }
}

#endif

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint16_t rem;
        __disable_irq();
        rem = usTxCount - usTxDmaLen;      /* DMA 期间新追加的数据 */
        if (rem > 0)
        {
            memmove(xTxRing, xTxRing + usTxDmaLen, rem);  /* 紧凑到 0 对齐 */
            usTxCount = rem;
        }
        else
        {
            usTxCount = 0;
        }
        usTxDmaLen = 0;
        ucTxBusy = 0;
        __enable_irq();

        if (usTxCount > 0)
        {
            vUsart_Tx_Start();
        }
    }
}

/**
 * @brief 此函数用于从DMA串口环形缓冲读取已接收数据
 * @param pcDstBuffer 指向目标缓冲
 * @param ulSize 接收数据的最大大小
 * @return 0表示本次全部读出 1表示发生了数据丢失
 *         (待读数据多于 ulSize, 或消费落后一整圈被丢弃了最旧数据)
 * @note 可任意频率反复调用: 无新数据时立即返回0, 不会重复返回旧数据;
 *       环缓冲大小 RX_RING_SIZE 字节, 消费间隔超过一整圈数据量会丢数据(返回1)。
 */
uint8_t ucUsartReceiveData( char *pcDstBuffer, uint32_t uiSize)
{
    uint32_t ulAvail;
    uint32_t ulN;
    uint32_t ulPos;
    uint32_t ulFirst;
    uint8_t ucReturnVal = 0;

    /* 无符号差 = 模 2^32 的精确差, 前提是真实落后量 < 2^32:
       消费侧一旦落后超过整环就丢最旧并追平(下方分支), 故函数返回时落后恒
       <= RX_RING_SIZE, 回绕(约 4GB 数据量)不会造成错误。示例: uiRxTotal 回绕
       到 0x1000 而 uiRxConsumed=0xFFFFF000 时, 差值按模运算 = 2^32+0x1000-
       0xFFFFF000 = 0x2000, 与真实字节差一致。 */
    ulAvail = uiRxTotal - uiRxConsumed;
    if (ulAvail == 0)
    {
        return 0;
    }
    if (ulAvail > RX_RING_SIZE)
    {
        /* 消费落后一整圈以上: 环已被覆盖, 丢弃最旧部分, 报告丢失 */
        uiRxConsumed += ulAvail - RX_RING_SIZE;
        ulAvail = RX_RING_SIZE;
        ucReturnVal = 1;
    }
    ulN = (ulAvail > uiSize) ? uiSize : ulAvail;   /* 部分读取安全 */
    if (ulN < ulAvail)
    {
        ucReturnVal = 1;
    }

    ulPos = uiRxConsumed % RX_RING_SIZE;
    ulFirst = RX_RING_SIZE - ulPos;
    if (ulFirst > ulN)
    {
        ulFirst = ulN;
    }
    memcpy( pcDstBuffer, xRxRing + ulPos, ulFirst );
    if (ulN > ulFirst)
    {
        memcpy( pcDstBuffer + ulFirst, xRxRing, ulN - ulFirst );
    }

    uiRxConsumed += ulN;
    return ucReturnVal;
}

/* DMA 半/满事件: 用 (整环计数, NDTR) 快照精确结算, 无累计误差。
   满事件发生在 NDTR 重载(整环边界)之后, 先加整环计数再快照。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart ->Instance == USART1)
    {
        uiRxBlockCnt++;
        uiRxTotal = ucUsartRxSnap();

#if(USE_FREERTOS)
        BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xUsart1_Rx_Semaphore, &pxHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
#endif
    }
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    ( void )huart;
    uiRxTotal = ucUsartRxSnap();

#if(USE_FREERTOS)
    BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xUsart1_Rx_Semaphore, &pxHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
#endif
}

/* USART1 全局中断入口(由 stm32f4xx_it.c 的 USART1_IRQHandler 转发):
   在线路空闲(IDLE, 约一帧宽)时结算刚到的短数据, 之后把其余中断交给 HAL。 */
void vUsart1_IRQHandler( uint8_t isIDLE )
{	
    if ( isIDLE )
    {
        uint32_t ulNow;

        ulNow = ucUsartRxSnap();
        if ( ulNow != uiRxTotal )                 /* 忽略无新数据的空闲 */
        {
            uiRxTotal = ulNow;
        }
    }    
}

/* 错误(ORE/FE/NE等)回调: HAL 错误路径会终止 DMA 循环接收, 这里清标志后重启,
   否则串口接收会永久停摆。DMA 重启从环头写起, 因此计数归零与环对齐。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        uiRxTotal = 0;
        uiRxConsumed = 0;
        uiRxBlockCnt = 0;
        __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);   /* EndRxTransfer 不清 IDLEIE, 防御性重开 */
        HAL_UART_Receive_DMA(huart, xRxRing, RX_RING_SIZE);
    }
}

uint8_t Usart1_Return_Rx_FirstChar(void)
{
    return xRxRing[uiRxConsumed % RX_RING_SIZE];
}

/* 返回当前可读(未消费)字节数; 消费侧据此决定一次最多可安全拉取多少 */
uint32_t Usart1_Return_Rx_Avail(void)
{
    return uiRxTotal - uiRxConsumed;
}
