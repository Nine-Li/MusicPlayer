# MusicPlayer

基于 **STM32F407ZGTx** 的 FreeRTOS 音乐播放器：从 SD 卡读取 MP3，经 minimp3 解码后由 I2S + WM8978 输出，并提供一个串口命令行（ucmd）用于控制播放与调试。

## 功能特性

- FreeRTOS 多任务调度，中断/DMA 驱动，串口、SDIO、I2S 全部 DMA 化
- SD 卡 FAT/FAT32/exFAT 文件系统（FatFs R0.15，长文件名，UTF-8）
- MP3 软解码（minimp3），按帧采样率自动重配 PLLI2S / I2S
- WM8978 编解码器（I2C1 控制，I2S2 数据），耳机输出
- 串口命令行 shell（ucmd）：文件浏览、播放控制、示例命令
- 支持播放 / 暂停 / 恢复 / 停止

## 硬件平台

| 项目 | 说明 |
| --- | --- |
| MCU | STM32F407ZGTx，LQFP144 |
| 主频 | 168 MHz（HSE 25 MHz 晶振，PLLM=25 / PLLN=336 / PLLP=2 / PLLQ=7） |
| 调试 | SWD：PA13 / PA14 |
| 串口控制台 | USART1：PA9(TX) / PA10(RX)，115200-8N1 |
| SD 卡 | SDIO 4-bit：PC8-PC12、PD2 |
| 音频 | I2S2：PC6(MCLK)、PB12/PB13/PB15(WS/CK/SD)；WM8978 控制 I2C1：PB8(SCL)/PB9(SDA) |
| 指示灯 | LED：PF9 / PF10（低电平点亮）（！！！LED配置有误，不是PF9和PF10） |
| 存储 | Flash 1 MB @ 0x08000000；RAM 112 KB + 16 KB |

## 软件架构

启动流程：

1. `main()`：配置时钟、GPIO，初始化 I2C1 与 WM8978，初始化 I2S2，创建 `vInitTask`（优先级 15）并启动调度器。
2. `vInitTask` 按顺序完成外设与任务初始化后自删除：
   - 创建 USART1 接收信号量与 TX 守护任务 `uTx`
   - 创建 SD 卡守护任务 `SD_GateKeeper`（必须先于 `f_mount`）
   - 创建 I2S 守护任务 `I2S_GateKeeper`（必须先于任何播放请求）
   - 挂载文件系统 `f_mount(fs, "SD:", 1)`
   - 创建 `ucmd` 命令任务与 `Music_Daemon` 播放控制任务

主要任务：

| 任务 | 优先级 | 栈（字） | 职责 |
| --- | --- | --- | --- |
| `vInitTask` | 15 | 336 | 启动初始化，完成后自删除 |
| `uTx` | 15 | 192 | USART1 发送队列 → DMA |
| `SD_GateKeeper` | 15 | 512 | SD 读写请求队列的唯一消费者 |
| `I2S_GateKeeper` | 15 | 512 | I2S DMA 传输请求队列的唯一消费者 |
| `ucmd` | 14 | 512 | 串口命令解析与执行 |
| `Music_Daemon` | 14 | 512 | 播放控制（播放/暂停/恢复/停止） |
| `Music_Player` | 13 | 6144 | 按需创建，读取并解码 MP3，逐帧投递 I2S |

设计要点：SD 与 I2S 均采用“请求队列 + 守护任务”模型，避免在中断或业务任务中直接操作 DMA；音频采用双半缓冲（`pump.done[2]`）流水线，保证解码与播放并行。

## 目录结构

```
Core/                     应用与 CubeMX 生成代码（main / gpio / it / apptask）
Drivers/
  BSP/                    手写外设驱动（usart / sd2 / i2s / iic / wm8978）
  system/
    ucmd/                 串口命令框架
    ff16/                 FatFs + SD 平台适配（diskio / ff_platform / ff_app）
    MP3/                  MP3 播放驱动 + minimp3
    mmf/                  内存池（当前未启用）
  freeRTOS/               FreeRTOS 内核 V11.3.0
  STM32F4xx_HAL_Driver/   STM32 HAL 驱动
  CMSIS/                  CMSIS 内核与器件支持
MDK-ARM/                  Keil uVision 工程（MusicPlayer.uvprojx）
freeRTOS.ioc              CubeMX 工程（仅 NVIC / RCC / SYS）
```

> 注意：除 `Core/` 中 CubeMX 生成的 `main.c` / `gpio.c` / `stm32f4xx_it.c` 外，所有外设驱动均为手写，不在 `.ioc` 中管理。使用 CubeMX 重新生成会破坏这些手写内容。

## 编译与烧录

- 工具链：Keil MDK-ARM（ARMCC / AC5，C99，`-O1`）
- 工程文件：`MDK-ARM/MusicPlayer.uvprojx`，目标 `freeRTOS`
- 输出：`MDK-ARM/Output/`（`MusicPlayer.axf` / `.hex`）
- 使用 ST-Link / J-Link 经 SWD 烧录

## 串口命令（ucmd）

所有命令以 `>` 开头，命令名与参数以空格分隔；参数前可加 `-`，字符串参数支持 `"引号"`。

| 命令 | 原型 | 说明 |
| --- | --- | --- |
| `help` | `vUcmdGetHelp()` | 显示帮助 |
| `flist` | `vUcmdGetFunList()` | 列出全部已注册命令 |
| `echo` | `vCmdEcho(char *s)` | 回显字符串 |
| `led` | `vCmdLed(char c)` | `1` 点亮 PF10，其余熄灭 |
| `add` | `vCmdAdd(int a, int b)` | 打印两数之和 |
| `pwm` | `vCmdPwm(uint32_t ch, float duty)` | 打印通道与占空比 |
| `thermo` | `vCmdThermo(int s, double t)` | 打印温度值 |
| `open` | `open_file(char *path)` | 打开文件/切换目录（`f_chdir`） |
| `ls` | `list_curdir()` | 列出当前目录 |
| `plm` | `vMusic_Play(char *path)` | 播放指定 MP3 |
| `pam` | `vMusic_Pause()` | 暂停 |
| `rem` | `vMusic_Resume()` | 恢复 |
| `stm` | `vMusic_Stop()` | 停止 |

示例：

```
>ls
>open SD:/Music
>plm "SD:/Music/test.mp3"
>pam
>rem
>stm
```

## 音频链路

```
SD 卡 → FatFs → mp3_buf → minimp3 解码 → PCM 双缓冲
      → I2S_GateKeeper → HAL_I2S_Transmit_DMA → I2S2 → WM8978 → 耳机
```

- 仅支持**立体声** MP3；单声道文件会被判为错误。
- 支持的采样率：8k / 16k / 22.05k / 32k / 44.1k / 48k / 96k Hz；11.025k / 12k / 24k 不支持。
- I2S 内核时钟 = (HSE/PLLM) × PLLI2SN / PLLI2SR，PLLI2S 在 `HAL_I2S_MspInit` 与 `i2s_set_freq` 中按采样率重配。

## 已知约束

- 删除 SD 卡或在挂载后更换会导致文件操作失败，需复位重新挂载。
- `FF_FS_LOCK = 2`：同一卷最多同时跟踪两个打开对象（文件或子目录），打开第三个不同子目录会返回 `FR_TOO_MANY_OPEN_FILES(18)`。
- `Music_Player` 任务栈需保持 6144 字（`mp3dec_decode_frame` 使用约 16 KB 栈空间）。
- 源文件保持 **UTF-8 无 BOM**；注释可用中文，字符串字面量请保持 ASCII。
