#ifndef __WM8978_H
#define __WM8978_H

#include "main.h"

/*
	WM8978 寄存器通过 I2C1 配置, 器件地址为 8bit 写地址 0x34 (7bit 地址 0x1A)。
	使用前需先调用 iic1_init() 完成 I2C1 (PB8/PB9) 初始化。
*/
#define WM8978_ADDR                     0x34

/* HAL I2C 传输超时时间 (ms) */
#define WM8978_I2C_TIMEOUT              100

/* 输出音量范围 0-63 */
#define VOLUME_MAX                      63

/* MIC PGA 增益范围 0-63 */
#define GAIN_MAX                        63

/*
	WM8978 音频输入通道选择, 可用按位或组合: MIC_LEFT_ON | LINE_ON
*/
typedef enum
{
	IN_PATH_OFF   = 0x00,	/* 输入关闭 */
	MIC_LEFT_ON   = 0x01,	/* LIN,LIP 脚, MIC 输入 (接板载麦克风) */
	MIC_RIGHT_ON  = 0x02,	/* RIN,RIP 脚, MIC 输入 (接板载麦克风) */
	LINE_ON       = 0x04,	/* L2,R2 输入 (接板载线路输入) */
	AUX_ON        = 0x08,	/* AUXL,AUXR 输入 (本板未使用) */
	DAC_ON        = 0x10,	/* I2S 送入 DAC (CPU 播放音频信号) */
	ADC_ON        = 0x20	/* 输出音频到 WM8978 内部 ADC (I2S 录音) */
} IN_PATH_E;

/*
	WM8978 音频输出通道选择, 可用按位或组合
*/
typedef enum
{
	OUT_PATH_OFF  = 0x00,	/* 输出关闭 */
	EAR_LEFT_ON   = 0x01,	/* LOUT1 输出 (接板载耳机) */
	EAR_RIGHT_ON  = 0x02,	/* ROUT1 输出 (接板载耳机) */
	SPK_ON        = 0x04,	/* LOUT2,ROUT2 输出 (本板未使用) */
	OUT3_4_ON     = 0x08	/* OUT3, OUT4 输出 (本板未使用) */
} OUT_PATH_E;

uint8_t wm8978_Init(void);
uint8_t wm8978_Reset(void);
void wm8978_CfgAudioIF(uint16_t _usStandard, uint8_t _ucWordLen);
void wm8978_OutMute(uint8_t _ucMute);
void wm8978_PowerDown(void);
void wm8978_CfgAudioPath(uint16_t _InPath, uint16_t _OutPath);
void wm8978_SetMicGain(uint8_t _ucGain);
void wm8978_SetLineGain(uint8_t _ucGain);
void wm8978_SetOUT2Volume(uint8_t _ucVolume);
void wm8978_SetOUT1Volume(uint8_t _ucVolume);
uint8_t wm8978_ReadOUT1Volume(void);
uint8_t wm8978_ReadOUT2Volume(void);
void wm8978_NotchFilter(uint16_t _NFA0, uint16_t _NFA1);
void wm8978_CtrlGPIO1(uint8_t _ucValue);

#endif /* __WM8978_H */
