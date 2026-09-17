/**
  ******************************************************************************
  * @file    mp3_driver.c
  * @author  ucmd project
  * @brief   MP3 解码与播放驱动 (I2S2 -> WM8978)。
  *          本文件基于 minimp3 解码器播放 SD 卡中的 MP3 文件, 数据流为:
  *          FATFS 读取 -> minimp3 解码 -> I2S2 DMA -> WM8978 DAC。
  *
  @verbatim
  ==============================================================================
                        ##### MP3 驱动功能特性 #####
  ==============================================================================

  [..] 本驱动实现的功能:
      (+) 从 SD 卡读取 MP3 压缩数据流
      (+) 用 minimp3 解码为 16bit 立体声 PCM
      (+) 通过 I2S2 (DMA1_Stream4, DMA_NORMAL) 逐帧送 WM8978 播放
      (+) 依据帧头采样率自动重配 PLLI2S / I2S

  [..] 任务与并发模型:
      (#) mp3_play() 运行在 Music_Player 任务 (优先级 2), 负责读文件、解码,
          并把 PCM 播放请求投入 I2S_GateQueue。
      (#) I2S_GateKeeper 任务 (优先级 14) 是 I2S_GateQueue 的唯一消费者,
          负责启动 HAL_I2S_Transmit_DMA 并等待 DMA 完成中断。
      (#) 双缓冲: pump.done[2] 两个二值信号量分别表示两块 PCM 半缓冲是否空闲。

  [..] 关键约束:
      (#) PCM_BUF_SIZE 必须恰好容纳两帧 (两个半区交替使用), 预留量需小于一帧。
      (#) i2s_stop() 必须先排空在途传输再停 DMA, 因为 HAL_DMA_Abort 不会产生
          TxCplt 回调, 先 abort 会让 gatekeeper 永远等不到完成信号量。
      (#) 仅支持立体声, 单声道帧会被直接判为失败。

  @endverbatim
  ******************************************************************************
  * @attention
  *
  * 本项目自定义驱动, 遵循工程既有编码约定 (UTF-8 无 BOM)。
  ******************************************************************************
  */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "i2s_driver.h"
#include "wm8978_driver.h"
#include "usart_driver.h"
#include "mp3_driver.h"

#define MP3_BUF_SIZE            (13824)   // 压缩数据输入缓冲大小 (字节)
#define PCM_BUF_SIZE            ((1152 * sizeof(int16_t) * 2 * 2) + 128)   // 两帧立体声 PCM 缓冲 (字节), +128 预留

extern FATFS *fs;   // 由 vInitTask 挂载并持有的全局 FATFS 对象

/**
  * @brief  I2S 请求方向枚举。
  * @note   当前仅使用 I2SWrite; I2SRead 预留给未来的读取功能。
  */
typedef enum
{
    I2SWrite,   // 发送 (写)
    I2SRead     // 接收 (读)
}I2SReg_Dir_t;

/**
  * @brief  I2S 传输请求描述块, 通过 I2S_GateQueue 传递给 I2S_GateKeeper。
  */
typedef struct
{
    I2S_HandleTypeDef *hi2s;             // I2S 外设句柄 (此处为 i2s2_handle)
    I2SReg_Dir_t xI2SReg_Dir;            // 传输方向 (写/读)
    uint16_t *pcBuf;                     // 待发送 PCM 数据首地址 (半字指针)
    size_t xSize;                        // 待发送样本数 (单位: 半字, 非字节)
    uint8_t *pucI2SReg_Result;           // 传输结果输出 (0=成功, 1=失败)
    TickType_t WaitTick;                 // 等待 TxCplt 的超时时间 (单位: tick)
    SemaphoreHandle_t xSemaphoreHandle;  // 传输完成后归还的完成信号量
}I2SReg_t;

SemaphoreHandle_t I2S_IRQ_Semaphore = {0};   // DMA TxCplt 与 gatekeeper 之间的完成信号量
QueueHandle_t I2S_GateQueue = {0};           // I2S 传输请求队列, 由 I2S_GateKeeper 消费

static mp3_player_buf_typedef   mp3_player_buf_s = {0}; // 动态分配的输入/PCM 缓冲
static FIL                      fil = {0};              // FatFs 文件对象
static uint8_t                  opened = 0;             // 文件是否已打开 (1=退出时需 f_close)
static i2s_pump_t               pump = {0};             // 双缓冲泵状态

extern TaskHandle_t xMusic_DaemonHandle;

/**
  * @brief  初始化 MP3 解码器并分配解码所需缓冲。
  * @param  mp3dec           minimp3 解码器状态 (由调用者提供存储)。
  * @param  mp3_player_buf_s 输出: 压缩数据缓冲与 PCM 缓冲指针。
  * @retval 0  成功
  * @retval 1  内存分配失败, 或文件系统未挂载
  */
static char mp3_player_init(mp3dec_t *mp3dec, mp3_player_buf_typedef *mp3_player_buf_s)
{
    /** 复位解码器内部状态 */
    mp3dec_init(mp3dec);
    mp3_player_buf_s->mp3_buf =  (uint8_t *)pvPortMalloc(MP3_BUF_SIZE);   // 压缩数据输入缓冲
    mp3_player_buf_s->pcm = (int16_t *)pvPortMalloc(PCM_BUF_SIZE);        // 解码输出 PCM 缓冲

    /** 任一缓冲分配失败则返回错误 (由调用者统一释放) */
    if (!mp3_player_buf_s->mp3_buf || !mp3_player_buf_s->pcm)
    {
        return 1;
    }

    /** 校验文件系统是否已挂载 (fs 由 vInitTask 持有) */
    if (fs == NULL || fs->fs_type == 0)
    {
        iUsart1_Printf( "Failed to initialize the music player because no drive is mounted." );
        return 1;
    }

    return 0;
}

/**
  * @brief  释放 mp3_player_init 分配的两块缓冲。
  * @param  mp3_player_buf_s 缓冲描述块 (释放后指针置 NULL)。
  * @retval 无
  */
static void mp3_player_buf_free(mp3_player_buf_typedef *mp3_player_buf_s)
{
    /** 逐块释放, 对空指针安全 */
    if (mp3_player_buf_s->mp3_buf)
    {   
        vPortFree(mp3_player_buf_s->mp3_buf);
        mp3_player_buf_s->mp3_buf = NULL;       // 置空, 防止重复释放
    }
    if (mp3_player_buf_s->pcm)
    {
        vPortFree(mp3_player_buf_s->pcm);
        mp3_player_buf_s->pcm = NULL;           // 置空, 防止重复释放
    }
}


/**
  * @brief  安全停止 I2S DMA 播放。
  * @param  p 双缓冲泵状态 (pump)。
  * @note   先等待两个 done 令牌 (让在途传输自然结束) 再停 DMA。
  *         HAL_DMA_Abort 不产生 TxCplt 回调, 若先 abort 会使 gatekeeper
  *         永远等不到完成信号量而卡死。
  * @retval 无
  */
static void i2s_stop(i2s_pump_t *p)
{
    /** 仅在已启动时处理; 先排空在途请求 */
    if (p->started)
    {
        for (int i = 0; i < 2; i++)   // 逐个半缓冲
        {
            (void)xSemaphoreTake( p->done[i], portMAX_DELAY );  /* 等待在途的完成令牌 */
            p->result[i] = 0;
            xSemaphoreGive( p->done[i] );
        }
    }

    /** 在途传输已结束后再停 DMA, 此时 DMA 空闲, 不依赖 abort 回调 */
    if (p->started)
    {
        HAL_I2S_DMAStop( &i2s2_handle );
    }

    p->started = 0;
}

/**
  * @brief  解码并播放一个 MP3 文件 (阻塞直到播放结束或出错)。
  * @param  path 以 "SD:" 卷号开头的文件路径。
  * @retval 0 正常播放结束
  * @retval 1 出错 (打开/读取/分配失败, 单声道, 采样率不支持, 传输失败等)
  */
MP3_Status_t mp3_play(const char *path)
{
    static mp3dec_t         mp3dec;                   // 解码器状态 (~6.7 KB), 静态存储避免撑爆任务栈
    mp3dec_frame_info_t     info = {0};               // 当前帧信息 (采样率/声道/帧字节数)
    size_t mp3_pos = 0, mp3_size = 0, cur_hz = 0;     // 输入缓冲读位置/有效长度, 当前采样率
    size_t pcm_size = 0;                              // PCM 缓冲中已解码的有效字节数
    size_t pcm_pos = 0;                               // PCM 缓冲中已投递播放的字节数
    uint8_t pumpdoneIndex = 0;                        // 当前半缓冲索引 (0/1 交替)
    size_t pcm_dif = 0;                               // 本次可投递的 PCM 字节数
    MP3_Status_t rev = MP3_OK;                                     // 返回值 (0=正常结束, 1=出错)

    I2SReg_t I2SReg = {0};                            // 传输请求模板
    I2SReg.hi2s = &i2s2_handle;                       // 目标 I2S 句柄
    I2SReg.WaitTick = pdMS_TO_TICKS( 200 );           // 等待 TxCplt 的超时 (200ms)
    I2SReg.xI2SReg_Dir = I2SWrite;                    // 方向: 发送

    pump.done[0]  = xSemaphoreCreateBinary(  );
    pump.done[1]  = xSemaphoreCreateBinary(  );

    /** 任一创建失败则释放已创建的并返回 */
    if (!pump.done[0] || !pump.done[1])
    {
        rev = MP3_INIT_SEMAPHR_FAIL;
        return rev;
    }

    /** 初始时两块半缓冲都空闲 */
    xSemaphoreGive( pump.done[0] );
    xSemaphoreGive( pump.done[1] );

    pump.result[0]    = 0;
    pump.result[1]    = 0;
    pump.started  = 0;

    /** 初始化解码器与缓冲; 失败则释放资源后退出 */
    if ( mp3_player_init(&mp3dec, &mp3_player_buf_s) != 0)
    {
        rev = MP3_INIT_FAIL;
        goto close;
    }

    /** 打开目标文件; 失败则退出 */
    if (f_open(&fil, path, FA_READ) != FR_OK) 
    {
        rev = MP3_OPEN_FILE_FAIL;
        goto close;
    }
    opened = 1;

    /** 解静音, 准备输出 */
    wm8978_OutMute( 0 );

    while (1)
    {
        /** 输入缓冲全部消费完则回绕到头部 */
        /* 输入缓冲补满: 剩余数据 memmove 到头部再 f_read */
        if (mp3_pos == mp3_size) 
        { 
            mp3_pos = mp3_size = 0; 
        }

        /** 剩余不足一帧 (4608 字节) 时, 把残留数据搬到头部并继续读文件 */
        if (mp3_size - mp3_pos < 4608)
        {
            memmove(mp3_player_buf_s.mp3_buf, mp3_player_buf_s.mp3_buf + mp3_pos, mp3_size - mp3_pos);
            mp3_size -= mp3_pos; mp3_pos = 0;
            UINT rd;   // 本次 f_read 实际读到的字节数
					
			FRESULT re;
			(void)re;
			
			re = f_read(&fil, mp3_player_buf_s.mp3_buf + mp3_size,
                MP3_BUF_SIZE - mp3_size, &rd);

            /** 读取失败直接退出 */
            if (re)
            {
                rev = MP3_READ_FAIL_FAIL;
                goto close;
            }

            /** 读到 0 字节且缓冲已空, 说明到达文件末尾 */
            if (rd == 0 && mp3_size == 0) break;      /* EOF */
            mp3_size += rd;
        }

        /** PCM 缓冲全部投递完时, 若剩余空间不足一帧则回绕到头部 */
        if (pcm_size == pcm_pos)
        {
            if (PCM_BUF_SIZE - pcm_size < 4608)
            {
                pcm_size = pcm_pos = 0;
            }
            
        }
        else
        {
            //应该不会进入这个分支
            if (PCM_BUF_SIZE - pcm_size < 4608)
            {
                rev = MP3_ERROR;
                goto close;
            }
        }

        if (xMusic_DaemonHandle)
            vTaskSuspend( xMusic_DaemonHandle );

        /** 等待当前半缓冲空闲 (上一轮投递的传输已完成) */
        xSemaphoreTake( pump.done[pumpdoneIndex], portMAX_DELAY );

        /** 上一次传输失败: 归还令牌并退出 */
        if (pump.result[0] || pump.result[1])
        {
            xSemaphoreGive( pump.done[pumpdoneIndex] );
            rev = MP3_I2S_FAIL;
            goto close;
        }

        /* 解码一帧到 PCM 缓冲尾部 (保留尚未投递的残留数据) */
        int r = mp3dec_decode_frame(&mp3dec, mp3_player_buf_s.mp3_buf + mp3_pos,
                                    mp3_size - mp3_pos,
                                    mp3_player_buf_s.pcm + (pcm_size / sizeof(int16_t)),
                                    &info);

        /* 未解出有效帧: frame_bytes==0 表示数据不足/EOF, 否则为跳过的 ID3/非法数据 */
        if (r <= 0)
        {
            if (info.frame_bytes == 0)
            {
                xSemaphoreGive( pump.done[pumpdoneIndex] );
                break;
            }
            mp3_pos += info.frame_bytes;
            xSemaphoreGive( pump.done[pumpdoneIndex] );
            continue;
        }

        /* 仅支持立体声, 单声道帧判为失败 */
        if (info.channels == 1)
        {
            xSemaphoreGive( pump.done[pumpdoneIndex] );
            rev = MP3_CHANNELS_ERROR;
            goto close;
        }

        mp3_pos += info.frame_bytes;
        pcm_size += r * info.channels * sizeof(int16_t);

        /* 采样率变化: 停 DMA -> 重配 I2S, 下一轮重新启动 */
        if (info.hz != cur_hz)
        {   /* 采样率切换: 停 DMA -> 重配 I2S -> 下一轮 pump 会重新启动 */
            xSemaphoreGive( pump.done[pumpdoneIndex] );
            i2s_stop( &pump );
            xSemaphoreTake( pump.done[pumpdoneIndex], portMAX_DELAY );
            cur_hz = info.hz;

            /** 不支持的采样率: 归还令牌并退出 */
            if (i2s_set_freq( cur_hz ))
            {
                xSemaphoreGive( pump.done[pumpdoneIndex] );
                rev = MP3_FREQ_ERROR;
                goto close;
            }
        }

        pcm_dif = pcm_size - pcm_pos;

        /* 有可播放数据则投递一个请求给 gatekeeper */
        if (pcm_dif > 0)
        {
            I2SReg.pcBuf = (uint16_t *)mp3_player_buf_s.pcm + (pcm_pos / sizeof(int16_t));
            I2SReg.xSemaphoreHandle = pump.done[pumpdoneIndex];
            I2SReg.xSize = (pcm_dif) / sizeof(uint16_t);   // 字节数换算为半字数
            I2SReg.pucI2SReg_Result = pump.result + pumpdoneIndex;

            /* 投递失败 (队列满/超时): 归还令牌并退出 */
            if (xQueueSend( I2S_GateQueue, &I2SReg, pdMS_TO_TICKS( 200 ) ) != pdTRUE)
            {
                xSemaphoreGive( pump.done[pumpdoneIndex] );
                rev = MP3_I2S_FAIL;
                goto close;
            }
            else
            {
                pump.started = 1;
                pcm_pos += pcm_dif;         // 已投递字节数前移
                pumpdoneIndex++;            // 切到另一半缓冲
                pumpdoneIndex %= 2;

                if (xMusic_DaemonHandle)
                    vTaskResume( xMusic_DaemonHandle );
            }
        }
    }

close:
    if (xMusic_DaemonHandle)
        vTaskSuspend( xMusic_DaemonHandle );

    /* 停止 DMA (内部会先排空在途传输) */
    i2s_stop( &pump );

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

    if (opened)
    {
        f_close(&fil);
        opened = 0;
    }

    mp3_player_buf_free( &mp3_player_buf_s );

    if (xMusic_DaemonHandle)
        vTaskResume( xMusic_DaemonHandle );

    wm8978_OutMute( 1 );
    return rev;
}

/**
  * @brief  I2S DMA 发送完成回调 (DMA1_Stream4 TC 中断上下文)。
  * @param  hi2s 触发回调的 I2S 句柄。
  * @note   释放 I2S_IRQ_Semaphore, 唤醒正在等待的 I2S_GateKeeper。
  * @retval 无
  */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;   // 是否需要触发上下文切换

    /** 仅处理 SPI2(I2S2) 的完成事件 */
    if (hi2s->Instance == SPI2 && I2S_IRQ_Semaphore)
        xSemaphoreGiveFromISR( I2S_IRQ_Semaphore, &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

/**
  * @brief  I2S 传输仲裁任务: I2S_GateQueue 的唯一消费者。
  * @param  param 未使用。
  * @note   启动时创建 I2S_IRQ_Semaphore 与 I2S_GateQueue, 然后循环处理请求。
  * @retval 无 (永不返回)
  */
void I2S_GateKeeper( void *param )
{
    I2SReg_t I2SReg = {0};                                    // 从队列取出的请求
    I2S_IRQ_Semaphore = xSemaphoreCreateBinary(  );           // 创建 DMA 完成信号量
    I2S_GateQueue = xQueueCreate( 10, sizeof( I2SReg_t ) );   // 创建请求队列 (深度 10)

    while (1)
    {
        /** 阻塞等待一个传输请求 */
        xQueueReceive( I2S_GateQueue, &I2SReg, portMAX_DELAY );
        *I2SReg.pucI2SReg_Result = 0;   // 默认结果为成功

        if (I2SReg.xI2SReg_Dir == I2SWrite)
        {
            /** 启动 DMA 发送; 启动失败直接报错 */
            if (HAL_I2S_Transmit_DMA( I2SReg.hi2s, I2SReg.pcBuf, I2SReg.xSize ) != HAL_OK)
            {
                *I2SReg.pucI2SReg_Result = 1;
                xSemaphoreGive( I2SReg.xSemaphoreHandle );
            }
            /** 等待完成中断; 超时则报错并回收迟到的信号量 */
            else if ( xSemaphoreTake( I2S_IRQ_Semaphore, I2SReg.WaitTick ) != pdTRUE)
            {
                *I2SReg.pucI2SReg_Result = 1;
                xSemaphoreGive( I2SReg.xSemaphoreHandle );

                //丢失回调信号量
                xSemaphoreTake( I2S_IRQ_Semaphore, portMAX_DELAY );
            }
            else
            {
                *I2SReg.pucI2SReg_Result = 0;
                xSemaphoreGive( I2SReg.xSemaphoreHandle );
            }
        }
        else if ( I2SReg.xI2SReg_Dir == I2SRead )
        {
            //未使用I2S读取
            *I2SReg.pucI2SReg_Result = 1;
            xSemaphoreGive( I2SReg.xSemaphoreHandle );
        }
        else
        {
            *I2SReg.pucI2SReg_Result = 1;
            xSemaphoreGive( I2SReg.xSemaphoreHandle );
        }
    }
}

mp3_player_buf_typedef xMP3_Return_mp3_player_buf( void )
{
    return mp3_player_buf_s;
}

FIL xMP3_Return_fil( void )
{
    return fil;
}

uint8_t ucMP3_Return_opened( void )
{
    return opened;
}

i2s_pump_t ucMP3_Return_pump( void )
{
    return pump;
}

