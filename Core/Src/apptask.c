#include "apptask.h"
#include "gpio.h"
#include "sd2_driver.h"
#include "usart_driver.h"
#include "ff.h"
#include "ff_platform.h"
#include "ff_app.h"
#include "mp3_driver.h"
#include "ucmd.h"
#include "wm8978_driver.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "queue.h"

SemaphoreHandle_t xUsart1_Rx_Semaphore = {0};
QueueHandle_t XMusic_Daemon_Queue = {0};

TaskHandle_t xMusic_DaemonHandle = {0};

FATFS *fs = NULL;

void vInitTask( void *param)
{
    xUsart1_Rx_Semaphore = xSemaphoreCreateCounting( 10, 0 );
	
    vUsart1_Init();

    if (xTaskCreate( vUsart1_Tx_GateKeeper, "uTx", 192, NULL, 15, NULL ) != pdPASS)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        while (1);
    }

    if (xTaskCreate( vSD_GateKeeper, "SD_GateKeeper", 512, NULL, 15, NULL ) != pdPASS)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        iUsart1_Printf( "error: Failed to create SD_GateKeeper: not enough heap space.\n" );
        while (1);
    }
    if (xTaskCreate( I2S_GateKeeper, "I2S_GateKeeper", 512, NULL, 15, NULL ) != pdPASS)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        iUsart1_Printf( "error: Failed to create I2S_GateKeeper: not enough heap space.\n" );
        while (1);
    }

    fs = (FATFS *)pvPortMalloc(sizeof(FATFS));
    if (!fs)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        iUsart1_Printf( "error: Failed to create fs: not enough heap space.\n" );
        while (1);
    }
    f_mount(fs, "SD:", 1);

    if (xTaskCreate( vUcmd_Daemon, "ucmd", 512, NULL, 14, NULL) != pdPASS)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        iUsart1_Printf( "error: Failed to create ucmd: not enough heap space.\n" );
        while (1);
    }
    if (xTaskCreate( vMusic_Daemon, "Music_Daemon", 512, NULL, 14, &xMusic_DaemonHandle ) != pdPASS)
    {
        HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );
        iUsart1_Printf( "error: Failed to create Music_Daemon: not enough heap space.\n" );
        while (1);
    }
    vTaskDelete( NULL );
}

/* 拉取一批 RX 数据进命令缓冲并解析执行到排空。
   无数据时直接返回, 调用方回到信号量等待。 */
static void vUcmdDrainOnce(void)
{
    uint32_t ulAvail = Usart1_Return_Rx_Avail();
    uint16_t usPull = (uint16_t)usUcmdReturn_configUCMD_CMDBUF_SIZE();

    if (ulAvail == 0)
    {
        return;
    }
    if (ulAvail < usPull)
    {
        usPull = (uint16_t)ulAvail;
    }

    /* 无条件整块拉取: 无 '>' 的杂散数据由解析器丢弃, 不会残留饿死后续命令。
       ucCommandBuffer/usCommandBufferSize 由 ucmd.c 导出(见 ucmd.h)。 */
    ucUsartReceiveData((char *)ucCommandBuffer, usPull);
    usCommandBufferSize = usPull;

    do
    {
        vUcmdCommandParsing();
        vUcmdUserCommandExecution();
    } while (ucUcmdDataPending());
}

/* ucmd 唯一消费任务: 缓冲只被本任务写入/解析, 不存在跨任务覆盖竞态。
   每轮先主动排空(不依赖信号量令牌, 覆盖"启动前到站、ISR 未给信号量"
   与"计数信号量令牌合并丢失"两种情况), 再等待新数据事件。 */
void vUcmd_Daemon(void *vpParams)
{
    (void)vpParams;

    static UBaseType_t uxHighWaterMark = 0;
	(void)uxHighWaterMark;

    while (1)
    {
        xSemaphoreTake(xUsart1_Rx_Semaphore, portMAX_DELAY);
        vUcmdDrainOnce();
        uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    }
}

void vMusic_Play( char * Path )
{
    size_t i = 0;
    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    xMusic_Daemon_Reg.Music_Daemon_Cmd = Music_Play;

    for (i = 0; Path[i] != '\0'; i++)
    {
        xMusic_Daemon_Reg.Path[i] = Path[i];
    }
    Path[i] = '\0';

    xQueueSend( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, pdMS_TO_TICKS( 200 ) );
}
UCMD_W1( vMusic_Play, s )

void vMusic_Pause( void )
{
    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    xMusic_Daemon_Reg.Music_Daemon_Cmd = Music_Pause;
    xQueueSend( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, pdMS_TO_TICKS( 200 ) );
}
UCMD_W0( vMusic_Pause )

void vMusic_Resume( void )
{
    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    xMusic_Daemon_Reg.Music_Daemon_Cmd = Music_Resume;
    xQueueSend( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, pdMS_TO_TICKS( 200 ) );
}
UCMD_W0( vMusic_Resume )

void vMusic_Stop( void )
{
    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    xMusic_Daemon_Reg.Music_Daemon_Cmd = Music_Stop;
    xQueueSend( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, pdMS_TO_TICKS( 200 ) );
}
UCMD_W0( vMusic_Stop )

void vMusic_Daemon_Stop( TaskHandle_t *xPlayer_Task, char *pxMusic_Daemon_Played )
{
    mp3_player_buf_typedef mp3_player_buf = xMP3_Return_mp3_player_buf(  );
    FIL file = xMP3_Return_fil(  );
    uint8_t opened = ucMP3_Return_opened(  );
    i2s_pump_t pump = ucMP3_Return_pump(  );

    if (*xPlayer_Task)
    {
        vTaskDelete( *xPlayer_Task );
        *xPlayer_Task = NULL;
    }
    
    /** 仅在已启动时处理; 先排空在途请求 */
    if (pump.started)
    {
        for (int i = 0; i < 2; i++)   // 逐个半缓冲
        {
            (void)xSemaphoreTake( pump.done[i], portMAX_DELAY );  /* 等待在途的完成令牌 */
            pump.result[i] = 0;
            xSemaphoreGive( pump.done[i] );
        }
    }

    if (pump.done[0])
    {
        vSemaphoreDelete( pump.done[0] );
        pump.done[0] = NULL;
    }
    if (pump.done[1])
    {
        vSemaphoreDelete( pump.done[1] );
        pump.done[1] = NULL;
    }
    
    /** 在途传输已结束后再停 DMA, 此时 DMA 空闲, 不依赖 abort 回调 */
    if (pump.started)
    {
        HAL_I2S_DMAStop( &i2s2_handle );
    }

    if (opened)
    {
        f_close( &file );
        opened = 0;
    }

    if (mp3_player_buf.mp3_buf)
    {
        vPortFree( mp3_player_buf.mp3_buf );
        mp3_player_buf.mp3_buf = NULL;
    }
    if (mp3_player_buf.pcm)
    {
        vPortFree( mp3_player_buf.pcm );
        mp3_player_buf.pcm = NULL;
    }

		wm8978_OutMute( 1 );
    *pxMusic_Daemon_Played = 0;
}

void vMusic_Daemon( void *param )
{
    (void)param;
    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    TaskHandle_t xPlayer_Task = {0};
    char xMusic_Daemon_Played = 0;
    char xMusic_Daemon_PauseF = 0;

    XMusic_Daemon_Queue = xQueueCreate( 1, sizeof( Music_Daemon_Reg_t ) );

    while (1)
    {
        xQueueReceive( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, portMAX_DELAY );

        if (xMusic_Daemon_Reg.Music_Daemon_Cmd == Music_Play)
        {
            if (xMusic_Daemon_Played)
            {
                vMusic_Daemon_Stop( &xPlayer_Task, &xMusic_Daemon_Played );
            }
           
            if (xTaskCreate( vMusic_Player, "Music_Player", 6144, (void *)xMusic_Daemon_Reg.Path, 13, &xPlayer_Task ) != pdPASS)
            {
                iUsart1_Printf( "error: Failed to start playback: not enough heap space.\n" );
            }

            xMusic_Daemon_Played = 1;
        }
        else if (xMusic_Daemon_Reg.Music_Daemon_Cmd == Music_Pause)
        {
            if (!xMusic_Daemon_Played)
            {
                continue;
            }

            if (!xMusic_Daemon_PauseF)
            {
                vTaskSuspend( xPlayer_Task );
                xMusic_Daemon_PauseF = 1;
            }
        }
        else if (xMusic_Daemon_Reg.Music_Daemon_Cmd == Music_Resume)
        {
            if (!xMusic_Daemon_Played)
            {
                continue;
            }

            if (xMusic_Daemon_PauseF)
            {
                vTaskResume( xPlayer_Task );
                xMusic_Daemon_PauseF = 0;
            }
        }
        else if (xMusic_Daemon_Reg.Music_Daemon_Cmd == Music_Stop)
        {
            vMusic_Daemon_Stop( &xPlayer_Task, &xMusic_Daemon_Played );
        }
        else
        {
            iUsart1_Printf( "error:\nMusic Daemon Unrecognized command." );
        }
    }
}

void vMusic_Player( void *param)
{
    const char *Path = (char *)param;

    Music_Daemon_Reg_t xMusic_Daemon_Reg = {0};
    MP3_Status_t xMP3_Status = MP3_OK;

    xMP3_Status = mp3_play( Path );

    if (xMP3_Status != MP3_OK)
    {
			iUsart1_Printf( "mp3 play failed,error code:%d.\n", xMP3_Status );
    }

    xMusic_Daemon_Reg.Music_Daemon_Cmd = Music_Stop;
    xQueueSend( XMusic_Daemon_Queue, &xMusic_Daemon_Reg, pdMS_TO_TICKS( 200 ) );

    while (1);
}

/* configCHECK_FOR_STACK_OVERFLOW=2 的钩子: 在上下文切换临界区运行,
   不能调用会阻塞的打印/队列; 点灯 + 停住, 用调试器看 pcTaskName 定位。 */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    (void)xTask;
    (void)pcTaskName;

    HAL_GPIO_WritePin( GPIOF, GPIO_PIN_9, GPIO_PIN_RESET );

    taskDISABLE_INTERRUPTS();
    for( ;; );
}
