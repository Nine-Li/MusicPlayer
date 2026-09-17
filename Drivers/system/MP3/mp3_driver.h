#ifndef __MP3_H
#define __MP3_H

#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "ff.h"
#include "ff_platform.h"              /* 若需要 SD 相关 */

typedef enum
{
    MP3_OK,
    MP3_ERROR,
    MP3_INIT_SEMAPHR_FAIL,
    MP3_INIT_FAIL,
    MP3_OPEN_FILE_FAIL,
    MP3_READ_FAIL_FAIL,
    MP3_I2S_FAIL,
    MP3_CHANNELS_ERROR,
    MP3_FREQ_ERROR,
}MP3_Status_t;

/**
  * @brief  MP3 播放器动态缓冲描述块。
  */
typedef struct
{
    uint8_t *mp3_buf;   // 压缩数据输入缓冲 (MP3_BUF_SIZE 字节)
    int16_t *pcm;       // 解码输出 PCM 缓冲 (PCM_BUF_SIZE 字节)
} mp3_player_buf_typedef;

/**
  * @brief  I2S 双缓冲泵状态, 管理两块 PCM 半缓冲。
  */
typedef struct
{
    SemaphoreHandle_t done[2];   // done[i]: 第 i 块半缓冲空闲 (对应传输已完成)
    uint8_t           result[2]; // result[i]: 第 i 块半缓冲上次传输结果 (0=成功, 1=失败)
    uint8_t           started;   // 是否已启动 (1=已向 gatekeeper 投递过请求)
} i2s_pump_t;

MP3_Status_t mp3_play(const char *path);
void I2S_GateKeeper( void *param );
mp3_player_buf_typedef xMP3_Return_mp3_player_buf( void );
FIL xMP3_Return_fil( void );
uint8_t ucMP3_Return_opened( void );
i2s_pump_t ucMP3_Return_pump( void );

#endif
