/**
  ******************************************************************************
  * @file    wm8978_driver.c
  * @author  ucmd project
  * @brief   WM8978 音频编解码器 I2C 控制驱动 (HAL 版)。
  *          本文件参照野火 bsp_wm8978.c 的思路, 使用 STM32 HAL 库重写 WM8978
  *          的寄存器读写、音量/增益、音频接口、音频通路等控制函数。
  *
  @verbatim
  ==============================================================================
                        ##### WM8978 驱动说明 #####
  ==============================================================================

  [..] 硬件连接:
      (+) WM8978 的 I2C 控制口挂在 I2C1 (SCL=PB8, SDA=PB9)
      (+) 使用前必须先调用 iic1_init() 初始化 I2C1

  [..] 寄存器写时序 (WM8978 datasheet REGISTER MAP):
      (#) 第 1 字节 = (寄存器地址 << 1) | 寄存器值 bit8
      (#) 第 2 字节 = 寄存器值 bit7:0
      (#) 因此不能直接用 HAL_I2C_Mem_Write, 而用 HAL_I2C_Master_Transmit
          发送 2 字节。

  [..] 寄存器读:
      (#) WM8978 的 I2C 接口不支持读回寄存器, 故在 RAM 中维护一份寄存器
          镜像 wm8978_RegCash[], 写寄存器时同步更新, 读寄存器时直接返回镜像值。

  @endverbatim
  ******************************************************************************
  * @attention
  *
  * 本项目自定义驱动, 遵循工程既有编码约定 (UTF-8 无 BOM)。
  ******************************************************************************
  */
#include "wm8978_driver.h"
#include "main.h"

/* 内部函数声明 */
static uint8_t  WM8978_I2C_WriteRegister(uint8_t RegisterAddr, uint16_t RegisterValue);
static uint16_t wm8978_ReadReg(uint8_t _ucRegAddr);
static uint8_t  wm8978_WriteReg(uint8_t _ucRegAddr, uint16_t _usValue);

/*
	wm8978 寄存器镜像。
	由于 WM8978 的 I2C 接口不支持读回寄存器, 所以写寄存器时同步更新镜像,
	读寄存器时直接返回镜像中的值。
	寄存器 MAP 见 WM8978(V4.5_2011).pdf 第 89 页 (寄存器地址 7bit, 寄存器数据 9bit)。
*/
static uint16_t wm8978_RegCash[] = {
	0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
	0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
	0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
	0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
	0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
	0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
	0x001, 0x001
};

/**
  * @brief  通过 I2C1 向 WM8978 写入一个寄存器 (2 字节时序)。
  * @param  RegisterAddr  寄存器地址 (0-57)。
  * @param  RegisterValue 寄存器值 (9bit)。
  * @retval 1 通信成功
  * @retval 0 通信失败 (HAL I2C 返回错误)
  */
static uint8_t WM8978_I2C_WriteRegister(uint8_t RegisterAddr, uint16_t RegisterValue)
{
	uint8_t ucBuf[2];

	/* 第 1 字节: 寄存器地址 << 1, 最低位放寄存器值的 bit8 */
	ucBuf[0] = (uint8_t)(((RegisterAddr << 1) & 0xFE) | ((RegisterValue >> 8) & 0x01));
	/* 第 2 字节: 寄存器值的低 8 位 */
	ucBuf[1] = (uint8_t)(RegisterValue & 0xFF);

	/* WM8978_ADDR 为 8bit 写地址 (0x34), 直接作为 HAL 的 DevAddress 使用 */
	if (HAL_I2C_Master_Transmit(&iic1_handle, WM8978_ADDR, ucBuf, 2, WM8978_I2C_TIMEOUT) != HAL_OK)
	{
		return 0;
	}

	return 1;
}

/**
  * @brief  从寄存器镜像中读取 wm8978 寄存器值。
  * @param  _ucRegAddr 寄存器地址。
  * @retval 寄存器值
  */
static uint16_t wm8978_ReadReg(uint8_t _ucRegAddr)
{
	return wm8978_RegCash[_ucRegAddr];
}

/**
  * @brief  写 wm8978 寄存器 (同时更新寄存器镜像)。
  * @param  _ucRegAddr 寄存器地址。
  * @param  _usValue   寄存器值。
  * @retval 0 写入失败
  *         1 写入成功
  */
static uint8_t wm8978_WriteReg(uint8_t _ucRegAddr, uint16_t _usValue)
{
	uint8_t res;

	res = WM8978_I2C_WriteRegister(_ucRegAddr, _usValue);
	wm8978_RegCash[_ucRegAddr] = _usValue;

	return res;
}

/**
  * @brief  初始化 WM8978, 复位所有寄存器到默认状态。
  * @note   I2C1 需由调用者提前通过 iic1_init() 初始化。
  * @param  无
  * @retval 1 初始化成功
  *         0 初始化失败
  */
uint8_t wm8978_Init(void)
{
	return wm8978_Reset();	/* 复位 WM8978, 所有寄存器恢复默认状态 */
}

/**
  * @brief  修改输出通道 1 音量。
  * @param  _ucVolume 音量值, 0-63
  * @retval 无
  */
void wm8978_SetOUT1Volume(uint8_t _ucVolume)
{
	uint16_t regL;
	uint16_t regR;

	if (_ucVolume > VOLUME_MAX)
	{
		_ucVolume = VOLUME_MAX;
	}

	regL = _ucVolume;
	regR = _ucVolume;

	/*
		R52 LOUT1 Volume control
		R53 ROUT1 Volume control
	*/
	/* 先更新左声道音量值 */
	wm8978_WriteReg(52, regL | 0x00);

	/* 再同时更新右声道 (bit8=1 时两个声道一起更新) */
	wm8978_WriteReg(53, regR | 0x100);	/* 0x180 表示两个声道同时更新 */
}

/**
  * @brief  修改输出通道 2 音量。
  * @param  _ucVolume 音量值, 0-63
  * @retval 无
  */
void wm8978_SetOUT2Volume(uint8_t _ucVolume)
{
	uint16_t regL;
	uint16_t regR;

	if (_ucVolume > VOLUME_MAX)
	{
		_ucVolume = VOLUME_MAX;
	}

	regL = _ucVolume;
	regR = _ucVolume;

	/*
		R54 LOUT2 (SPK) Volume control
		R55 ROUT2 (SPK) Volume control
	*/
	/* 先更新左声道音量值 */
	wm8978_WriteReg(54, regL | 0x00);

	/* 再同时更新右声道 (bit8=1 时两个声道一起更新) */
	wm8978_WriteReg(55, regR | 0x100);
}

/**
  * @brief  读取输出通道 1 音量。
  * @param  无
  * @retval 当前音量值 (0-63)
  */
uint8_t wm8978_ReadOUT1Volume(void)
{
	return (uint8_t)(wm8978_ReadReg(52) & 0x3F);
}

/**
  * @brief  读取输出通道 2 音量。
  * @param  无
  * @retval 当前音量值 (0-63)
  */
uint8_t wm8978_ReadOUT2Volume(void)
{
	return (uint8_t)(wm8978_ReadReg(54) & 0x3F);
}

/**
  * @brief  设置静音。
  * @param  _ucMute 模式选择
  *         @arg 1 静音
  *         @arg 0 取消静音
  * @retval 无
  */
void wm8978_OutMute(uint8_t _ucMute)
{
	uint16_t usRegValue;

	if (_ucMute == 1)	/* 静音 */
	{
		usRegValue = wm8978_ReadReg(52);	/* Left Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(52, usRegValue);

		usRegValue = wm8978_ReadReg(53);	/* Left Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(53, usRegValue);

		usRegValue = wm8978_ReadReg(54);	/* Right Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(54, usRegValue);

		usRegValue = wm8978_ReadReg(55);	/* Right Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(55, usRegValue);
	}
	else	/* 取消静音 */
	{
		usRegValue = wm8978_ReadReg(52);
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(52, usRegValue);

		usRegValue = wm8978_ReadReg(53);	/* Left Mixer Control */
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(53, usRegValue);

		usRegValue = wm8978_ReadReg(54);
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(54, usRegValue);

		usRegValue = wm8978_ReadReg(55);	/* Right Mixer Control */
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(55, usRegValue);
	}
}

/**
  * @brief  设置 MIC 增益。
  * @param  _ucGain 增益值, 0-63
  * @retval 无
  */
void wm8978_SetMicGain(uint8_t _ucGain)
{
	if (_ucGain > GAIN_MAX)
	{
		_ucGain = GAIN_MAX;
	}

	/* PGA 增益控制 R45, R46
		Bit8 INPPGAUPDATE
		Bit7 INPPGAZCL   过零更新
		Bit6 INPPGAMUTEL PGA 静音
		Bit5:0 增益值 (010000 为 0dB)
	*/
	wm8978_WriteReg(45, _ucGain);
	wm8978_WriteReg(46, _ucGain | (1 << 8));
}

/**
  * @brief  设置 Line 输入通道的增益。
  * @param  _ucGain 增益值, 0-7。7 最大, 0 最小, 可衰减可放大
  * @retval 无
  */
void wm8978_SetLineGain(uint8_t _ucGain)
{
	uint16_t usRegValue;

	if (_ucGain > 7)
	{
		_ucGain = 7;
	}

	/*
		Mic 输入通道增益控制由 PGABOOSTL / PGABOOSTR 控制
		Aux 输入通道增益控制由 AUXL2BOOSTVO[2:0] / AUXR2BOOSTVO[2:0] 控制
		Line 输入通道增益控制由 LIP2BOOSTVOL[2:0] / RIP2BOOSTVOL[2:0] 控制
	*/
	/*	R47 左输入通道, R48 右输入通道, MIC 输入控制寄存器
		R47 (R48 内容相同)
		B8    PGABOOSTL = 1, 0 表示 MIC 信号直通无增益, 1 表示 MIC 信号 +20dB 增益
		B7    = 0 保留
		B6:4  L2_2BOOSTVOL = x, 0 表示禁止, 1-7 表示增益 -12dB ~ +6dB
		B3    = 0 保留
		B2:0  AUXL2BOOSTVOL = x, 0 表示禁止, 1-7 表示增益 -12dB ~ +6dB
	*/
	usRegValue = wm8978_ReadReg(47);
	usRegValue &= 0x8F;	/* 把 Bit6:4 清 0 (1000 1111) */
	usRegValue |= (_ucGain << 4);
	wm8978_WriteReg(47, usRegValue);

	usRegValue = wm8978_ReadReg(48);
	usRegValue &= 0x8F;
	usRegValue |= (_ucGain << 4);
	wm8978_WriteReg(48, usRegValue);
}

/**
  * @brief  关闭 wm8978, 进入掉电模式。
  * @param  无
  * @retval 无
  */
void wm8978_PowerDown(void)
{
	wm8978_Reset();	/* 复位 WM8978, 所有寄存器恢复默认状态 */
}

/**
  * @brief  配置 WM8978 的音频接口 (I2S)。
  * @param  _usStandard 接口标准: I2S_STANDARD_PHILIPS / I2S_STANDARD_MSB /
  *                     I2S_STANDARD_LSB (PCM 模式也支持)。
  * @param  _ucWordLen  字长: 16 / 24 / 32 (本驱动未使用 20bit 格式)。
  * @retval 无
  */
void wm8978_CfgAudioIF(uint16_t _usStandard, uint8_t _ucWordLen)
{
	uint16_t usReg;

	/* WM8978(V4.5_2011).pdf 73 页, 寄存器列表 */

	/*	REG R4, 音频接口控制寄存器
		B8    BCP  = X, BCLK 极性 (0 表示正常, 1 表示翻转)
		B7    LRCP = X, LRC 时钟极性 (0 表示正常, 1 表示翻转)
		B6:5  WL = X, 字长 00=16bit, 01=20bit, 10=24bit, 11=32bit
		B4:3  FMT = X, 音频数据格式 00=右对齐, 01=左对齐, 10=I2S 格式, 11=PCM
		B2    DACLRSWAP = X, DAC 数据输出到 LRC 的左边或右边
		B1    ADCLRSWAP = X, ADC 数据输出到 LRC 的左边或右边
		B0    MONO = 0, 0 表示立体声, 1 表示单声道
	*/
	usReg = 0;
	if (_usStandard == I2S_STANDARD_PHILIPS)	/* I2S 格式标准 */
	{
		usReg |= (2 << 3);
	}
	else if (_usStandard == I2S_STANDARD_MSB)	/* MSB 格式标准 (左对齐) */
	{
		usReg |= (1 << 3);
	}
	else if (_usStandard == I2S_STANDARD_LSB)	/* LSB 格式标准 (右对齐) */
	{
		usReg |= (0 << 3);
	}
	else	/* PCM 标准 (16 位通道帧, 上升沿采样, 同步下降沿, 16 位数据帧扩展为 32 位通道帧) */
	{
		usReg |= (3 << 3);
	}

	if (_ucWordLen == 24)
	{
		usReg |= (2 << 5);
	}
	else if (_ucWordLen == 32)
	{
		usReg |= (3 << 5);
	}
	else
	{
		usReg |= (0 << 5);		/* 16bit */
	}
	wm8978_WriteReg(4, usReg);

	/*
		R6 时钟控制寄存器
		MS = 0, WM8978 从机时钟, 由 MCU 提供 MCLK 时钟
	*/
	wm8978_WriteReg(6, 0x000);
}

/**
  * @brief  配置 wm8978 音频通路。
  * @param  _InPath  音频输入通路选择 (IN_PATH_E 的按位或)。
  * @param  _OutPath 音频输出通路选择 (OUT_PATH_E 的按位或)。
  * @retval 无
  */
void wm8978_CfgAudioPath(uint16_t _InPath, uint16_t _OutPath)
{
	uint16_t usReg;

	/* 查看 WM8978 数据手册 REGISTER MAP 章节, 第 89 页 */

	if ((_InPath == IN_PATH_OFF) && (_OutPath == OUT_PATH_OFF))
	{
		wm8978_PowerDown();
		return;
	}

	/*
		R1 寄存器 Power manage 1
		Bit8    BUFDCOPEN,  Output stage 1.5xAVDD/2 driver enable
		Bit7    OUT4MIXEN,  OUT4 mixer enable
		Bit6    OUT3MIXEN,  OUT3 mixer enable
		Bit5    PLLEN, PLL 使能
		Bit4    MICBEN, Microphone Bias Enable (MIC 偏置电路使能)
		Bit3    BIASEN, Analogue amplifier bias control (置 1 时模拟放大器工作)
		Bit2    BUFIOEN, Unused input/output tie off buffer enable
		Bit1:0  VMIDSEL, 设置为非 00 值模拟放大器工作
	*/
	usReg = (1 << 3) | (3 << 0);
	if (_OutPath & OUT3_4_ON)	/* OUT3 和 OUT4 使能, 输出给 GSM 模块 */
	{
		usReg |= ((1 << 7) | (1 << 6));
	}
	if ((_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= (1 << 4);
	}
	wm8978_WriteReg(1, usReg);

	/*
		R2 寄存器 Power manage 2
		Bit8    ROUT1EN,  ROUT1 output enable 右输出通道使能
		Bit7    LOUT1EN,  LOUT1 output enable 左输出通道使能
		Bit6    SLEEP, 0 = Normal device operation, 1 = standby
		Bit5    BOOSTENR, Right channel Input BOOST enable 右通道抬高电路使能
		Bit4    BOOSTENL, Left channel Input BOOST enable
		Bit3    INPGAENR, Right channel input PGA enable 右输入 PGA 使能
		Bit2    INPGAENL, Left channel input PGA enable
		Bit1    ADCENR, Enable ADC right channel
		Bit0    ADCENL, Enable ADC left channel
	*/
	usReg = 0;
	if (_OutPath & EAR_LEFT_ON)
	{
		usReg |= (1 << 7);
	}
	if (_OutPath & EAR_RIGHT_ON)
	{
		usReg |= (1 << 8);
	}
	if (_InPath & MIC_LEFT_ON)
	{
		usReg |= ((1 << 4) | (1 << 2));
	}
	if (_InPath & MIC_RIGHT_ON)
	{
		usReg |= ((1 << 5) | (1 << 3));
	}
	if (_InPath & LINE_ON)
	{
		usReg |= ((1 << 4) | (1 << 5));
	}
	if (_InPath & ADC_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(2, usReg);

	/*
		R3 寄存器 Power manage 3
		Bit8    OUT4EN,  OUT4 enable
		Bit7    OUT3EN,  OUT3 enable
		Bit6    LOUT2EN, LOUT2 output enable
		Bit5    ROUT2EN, ROUT2 output enable
		Bit4    0
		Bit3    RMIXEN, Right mixer enable
		Bit2    LMIXEN, Left mixer enable
		Bit1    DACENR, Right channel DAC enable
		Bit0    DACENL, Left channel DAC enable
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= ((1 << 8) | (1 << 7));
	}
	if (_OutPath & SPK_ON)
	{
		usReg |= ((1 << 6) | (1 << 5));
	}
	if (_OutPath != OUT_PATH_OFF)
	{
		usReg |= ((1 << 3) | (1 << 2));
	}
	if (_InPath & DAC_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(3, usReg);

	/*
		R44 寄存器 Input ctrl
		Bit8    MBVSEL, Microphone Bias Voltage Control 0 = 0.9 * AVDD, 1 = 0.6 * AVDD
		Bit7    0
		Bit6    R2_2INPPGA, Connect R2 pin to right channel input PGA positive terminal
		Bit5    RIN2INPPGA, Connect RIN pin to right channel input PGA negative terminal
		Bit4    RIP2INPPGA, Connect RIP pin to right channel input PGA positive terminal
		Bit3    0
		Bit2    L2_2INPPGA, Connect L2 pin to left channel input PGA positive terminal
		Bit1    LIN2INPPGA, Connect LIN pin to left channel input PGA negative terminal
		Bit0    LIP2INPPGA, Connect LIP pin to left channel input PGA positive terminal
	*/
	usReg = 0 << 8;
	if (_InPath & LINE_ON)
	{
		usReg |= ((1 << 6) | (1 << 2));
	}
	if (_InPath & MIC_RIGHT_ON)
	{
		usReg |= ((1 << 5) | (1 << 4));
	}
	if (_InPath & MIC_LEFT_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(44, usReg);

	/*
		R14 寄存器 ADC Control
		设置高通滤波器相关选择, WM8978(V4.5_2011).pdf 31 32 页
		Bit8    HPFEN, High Pass Filter Enable 高通滤波器使能, 0 表示禁止, 1 表示使能
		Bit7    HPFAPP, Select audio mode or application mode
		Bit6:4  HPFCUT, Application mode cut-off frequency
		Bit3    ADCOSR, ADC oversample rate select: 0=64x, 1=128x
		Bit2    0
		Bit1    ADC right channel polarity adjust
		Bit0    ADC left channel polarity adjust
	*/
	if (_InPath & ADC_ON)
	{
		usReg = (1 << 3) | (0 << 8) | (4 << 0);		/* 使能 ADC 高通滤波器, 设置截止频率 */
	}
	else
	{
		usReg = 0;
	}
	wm8978_WriteReg(14, usReg);

	/* 陷波滤波器 (notch filter) 用于抑制麦克风啸叫, 此处关闭
		R27, R28, R29, R30 用于控制陷波滤波器, WM8978(V4.5_2011).pdf 33 页
		R7 Bit7 NFEN = 0 表示禁止, 1 表示使能
	*/
	if (_InPath & ADC_ON)
	{
		usReg = (0 << 7);
		wm8978_WriteReg(27, usReg);
		usReg = 0;
		wm8978_WriteReg(28, usReg);
		wm8978_WriteReg(29, usReg);
		wm8978_WriteReg(30, usReg);
	}

	/* 自动增益控制 ALC, R32 - R34, WM8978(V4.5_2011).pdf 36 页 */
	{
		usReg = 0;		/* 禁止自动增益控制 */
		wm8978_WriteReg(32, usReg);
		wm8978_WriteReg(33, usReg);
		wm8978_WriteReg(34, usReg);
	}

	/*  R35 ALC Noise Gate Control
		Bit3    NGATEN, Noise gate function enable
		Bit2:0  Noise gate threshold
	*/
	usReg = (3 << 1) | (7 << 0);
	wm8978_WriteReg(35, usReg);

	/*
		Mic 输入通道增益控制由 PGABOOSTL / PGABOOSTR 控制
		Aux 输入通道增益控制由 AUXL2BOOSTVO[2:0] / AUXR2BOOSTVO[2:0] 控制
		Line 输入通道增益控制由 LIP2BOOSTVOL[2:0] / RIP2BOOSTVOL[2:0] 控制
	*/
	/*	WM8978(V4.5_2011).pdf 29 页, R47 左输入通道, R48 右输入通道
		B8    PGABOOSTL = 1, 0 表示 MIC 信号直通无增益, 1 表示 MIC 信号 +20dB 增益
		B7    = 0 保留
		B6:4  L2_2BOOSTVOL = x, 0 表示禁止, 1-7 表示增益 -12dB ~ +6dB
		B3    = 0 保留
		B2:0  AUXL2BOOSTVOL = x, 0 表示禁止, 1-7 表示增益 -12dB ~ +6dB
	*/
	usReg = 0;
	if ((_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= (1 << 8);	/* MIC 输入取 +20dB */
	}
	if (_InPath & AUX_ON)
	{
		usReg |= (3 << 0);	/* Aux 输入固定取 3 倍 */
	}
	if (_InPath & LINE_ON)
	{
		usReg |= (3 << 4);	/* Line 输入固定取 3 倍 */
	}
	wm8978_WriteReg(47, usReg);
	wm8978_WriteReg(48, usReg);

	/* 设置 ADC 输入控制, pdf 35 页
		R15 左输入通道 ADC 控制, R16 右输入通道 ADC 控制
		Bit8    ADCVU = 1 时才更新, 用于同步两个声道的 ADC 音量
		Bit7:0  音量选择 0000 0000 = 静音, 0000 0001 = -127dB,
		        0000 0010 = -12.5dB 按 0.5dB 递增, 1111 1111 = 0dB
	*/
	usReg = 0xFF;
	wm8978_WriteReg(15, usReg);	/* 选择 0dB 并更新左声道 */
	usReg = 0x1FF;
	wm8978_WriteReg(16, usReg);	/* 同步更新右声道 */

	/* 通过 wm8978_SetMicGain 单独控制 mic PGA 增益 */

	/*	R43 寄存器 AUXR - ROUT2 BEEP Mixer Function
		B8:6 = 0
		B5    MUTERPGA2INV, Mute input to INVROUT2 mixer
		B4    INVROUT2, Invert ROUT2 output 反相输出
		B3:1  BEEPVOL = 7, AUXR input to ROUT2 inverter gain
		B0    BEEPEN = 1, Enable AUXR beep input
	*/
	usReg = 0;
	if (_OutPath & SPK_ON)
	{
		usReg |= (1 << 4);	/* ROUT2 反相, 用于驱动扬声器 */
	}
	if (_InPath & AUX_ON)
	{
		usReg |= ((7 << 1) | (1 << 0));
	}
	wm8978_WriteReg(43, usReg);

	/* R49  Output ctrl
		B8:7  0
		B6    DACL2RMIX, Left DAC output to right output mixer
		B5    DACR2LMIX, Right DAC output to left output
		B4    OUT4BOOST
		B3    OUT3BOOST
		B2    SPKBOOST
		B1    TSDEN, Thermal Shutdown Enable 过热保护使能, 缺省 1
		B0    VROI, Disabled Outputs to VREF Resistance
	*/
	usReg = 0;
	if (_InPath & DAC_ON)
	{
		usReg |= ((1 << 6) | (1 << 5));
	}
	if (_OutPath & SPK_ON)
	{
		usReg |= ((1 << 2) | (1 << 1));	/* SPK 1.5x 增益, 过热保护使能 */
	}
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= ((1 << 4) | (1 << 3));	/* BOOT3 BOOT4 1.5x 增益 */
	}
	wm8978_WriteReg(49, usReg);

	/*	REG 50 (50 左声道, 51 右声道, 两个寄存器配置一样) WM8978(V4.5_2011).pdf 56 页
		B8:6  AUXLMIXVOL = 111, AUX 输入到 FM 输出混音
		B5    AUXL2LMIX = 1, Left Auxiliary input to left channel
		B4:2  BYPLMIXVOL 保留
		B1    BYPL2LMIX = 0, Left bypass path to left output mixer
		B0    DACL2LMIX = 1, Left DAC output to left output mixer
	*/
	usReg = 0;
	if (_InPath & AUX_ON)
	{
		usReg |= ((7 << 6) | (1 << 5));
	}
	if ((_InPath & LINE_ON) || (_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= ((7 << 2) | (1 << 1));
	}
	if (_InPath & DAC_ON)
	{
		usReg |= (1 << 0);
	}
	wm8978_WriteReg(50, usReg);
	wm8978_WriteReg(51, usReg);

	/*	R56 寄存器 OUT3 mixer ctrl
		B8:7  0
		B6    OUT3MUTE, 0 = Output stage outputs OUT3 mixer, 1 = muted
		B5:4  0
		B3    BYPL2OUT3, OUT4 mixer output to OUT3
		B2    LMIX2OUT3, Left ADC input to OUT3
		B1    LDAC2OUT3, Left DAC mixer to OUT3
		B0    LDAC2OUT3, Left DAC output to OUT3
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= (1 << 3);
	}
	wm8978_WriteReg(56, usReg);

	/* R57 寄存器 OUT4 (MONO) mixer ctrl
		B8:7  0
		B6    OUT4MUTE, 0 = Output stage outputs OUT4 mixer, 1 = muted
		B5    HALFSIG, 0 = OUT4 normal output, 1 = OUT4 attenuated by 6dB
		B4    LMIX2OUT4, Left DAC mixer to OUT4
		B3    LDAC2UT4, Left DAC to OUT4
		B2    BYPR2OUT4, Right ADC input to OUT4
		B1    RMIX2OUT4, Right DAC mixer to OUT4
		B0    RDAC2OUT4, Right DAC output to OUT4
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= ((1 << 4) | (1 << 1));
	}
	wm8978_WriteReg(57, usReg);

	/* R11, 12 寄存器 DAC 数字音量
		R11 Left DAC Digital Volume
		R12 Right DAC Digital Volume
	*/
	if (_InPath & DAC_ON)
	{
		wm8978_WriteReg(11, 255);
		wm8978_WriteReg(12, 255 | 0x100);
	}
	else
	{
		wm8978_WriteReg(11, 0);
		wm8978_WriteReg(12, 0 | 0x100);
	}

	/*	R10 寄存器 DAC Control
		B8    0
		B7    0
		B6    SOFTMUTE, Softmute enable
		B5    0
		B4    0
		B3    DACOSR128, DAC oversampling rate: 0=64x, 1=128x
		B2    AMUTE, Automute enable
		B1    DACPOLR, Right DAC output polarity
		B0    DACPOLL, Left DAC output polarity
	*/
	if (_InPath & DAC_ON)
	{
		wm8978_WriteReg(10, 0);
	}
}

/**
  * @brief  设置陷波滤波器 (notch filter), 用于抑制麦克风啸叫。
  * @param  _NFA0 NFA0[13:0] 系数
  * @param  _NFA1 NFA1[13:0] 系数
  * @retval 无
  */
void wm8978_NotchFilter(uint16_t _NFA0, uint16_t _NFA1)
{
	uint16_t usReg;

	/*  page 26
		A programmable notch filter is provided. This filter has a variable centre frequency and bandwidth,
		programmable via two coefficients, a0 and a1. a0 and a1 are represented by the register bits
		NFA0[13:0] and NFA1[13:0]. Because these coefficient values require four register writes to setup
		there is an NFU (Notch Filter Update) flag which should be set only when all four registers are setup.
	*/
	usReg = (1 << 7) | (_NFA0 & 0x3F);
	wm8978_WriteReg(27, usReg);

	usReg = ((_NFA0 >> 7) & 0x3F);
	wm8978_WriteReg(28, usReg);

	usReg = (_NFA1 & 0x3F);
	wm8978_WriteReg(29, usReg);

	usReg = (1 << 8) | ((_NFA1 >> 7) & 0x3F);
	wm8978_WriteReg(30, usReg);
}

/**
  * @brief  控制 WM8978 的 GPIO1 输出 0 或 1。
  * @param  _ucValue GPIO1 输出值, 0 或 1
  * @retval 无
  */
void wm8978_CtrlGPIO1(uint8_t _ucValue)
{
	uint16_t usRegValue;

	/* R8, pdf 62 页 */
	if (_ucValue == 0)	/* 输出 0 */
	{
		usRegValue = 6;	/* B2:0 = 110 */
	}
	else
	{
		usRegValue = 7;	/* B2:0 = 111 */
	}
	wm8978_WriteReg(8, usRegValue);
}

/**
  * @brief  复位 wm8978, 把所有寄存器值恢复到缺省值。
  * @param  无
  * @retval 1 复位成功
  *         0 复位失败
  */
uint8_t wm8978_Reset(void)
{
	/* wm8978 寄存器缺省值 */
	const uint16_t reg_default[] = {
	0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
	0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
	0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
	0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
	0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
	0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
	0x001, 0x001
	};
	uint8_t res;
	uint8_t i;

	res = wm8978_WriteReg(0x00, 0);

	for (i = 0; i < sizeof(reg_default) / sizeof(reg_default[0]); i++)
	{
		wm8978_RegCash[i] = reg_default[i];
	}

	return res;
}
