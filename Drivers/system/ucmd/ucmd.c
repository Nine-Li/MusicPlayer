#include "ucmd.h"
#include "ucmd_config.h"

/* ============================================================================
 * ucmd 串口命令框架 (Drivers/system/ucmd)
 *
 * 数据流(接收层尚未实现, 串口还没有配置):
 *   1. 接收层把收到的完整命令行写入 ucCommandBuffer, 字节数写入
 *      usCommandBufferSize(行格式见 ucmd.h);
 *   2. 调用 vUcmdCommandParsing()    把缓冲内命令行解析成 Cmd_t 并入队;
 *   3. 调用 vUcmdUserCommandExecution() 逐条取出并执行。
 * 解析器按行原位把参数文本切开补 '\0', Cmd_t::Argv[] 保存指向缓冲区的指针,
 * 因此队列中的数据在执行完成前不能被新的接收数据覆盖(队满时解析会暂停,
 * 剩余数据留到下次调用)。
 *
 * 参数的个数/类型/取值来源:
 *   - 个数: 由注册时的 UCMD_Wn() 宏形状决定, 运行时由包装器校验 Argc;
 *   - 类型: 由注册时的类型字母决定, 由 UCMD_CV_* 宏完成文本->类型转换;
 *   - 调用: 统一执行入口 CmdHandler_t 收到的是宏生成的包装器, 包装器用与真实
 *     函数完全匹配的函数指针类型转调, 编译器按 AAPCS 正确分配整数寄存器 r0-r3
 *     与 VFP 寄存器 s0-s3/d0-d3, 因此 int 与 float/double 混用也安全。
 * ==========================================================================*/

/* 缓冲数组 +1: 允许解析器在数据末尾原位写 '\0' */
uint8_t ucCommandBuffer[configUCMD_CMDBUF_SIZE + 1] = {0};
uint16_t usCommandBufferSize = 0;   /* 缓冲中有效数据字节数(接收层写入) */
static uint16_t usCommandBufferIndex = 0;   /* 解析游标 */

static Cmd_t xCommandList[configUCMD_CMDLIST_SIZE];   /* 待执行命令队列 */
static uint16_t usCommandListIndex = 0;   /* 队首(弹出位置) */
static uint16_t usCommandListSize = 0;    /* 队列当前条数 */

uint8_t *pucCommandBufferAddr = ucCommandBuffer;

/*----------------------------- 移植函数 ----------------------------------*/

/**
 * @brief 打印一个以 '\0' 结尾的字符串(输出移植点, 转发到串口)
 * @param Str 待打印的字符串指针
 * @return void
 */

extern int32_t iUsart1_Printf( const char *format, ... );

void UcmdPrintf( char *Str )
{
    iUsart1_Printf( Str );
}

/**
 * @brief 把整数按十进制文本打印到 UcmdPrintf(负数带 '-' 前缀)
 * @param lValue 待打印的有符号整数
 * @return void
 */
static void vUcmdPrintDec( int32_t lValue )
{
    char acBuf[16];
    uint16_t usIdx = 0;
    uint16_t usStart;
    uint32_t ulValue;
    char cTmp;

    if( lValue < 0 )
    {
        acBuf[usIdx++] = '-';
        ulValue = ( uint32_t )( -( ( int64_t )lValue ) );
    }
    else
    {
        ulValue = ( uint32_t )lValue;
    }

    usStart = usIdx;
    do
    {
        acBuf[usIdx++] = ( char )( '0' + ( int )( ulValue % 10 ) );
        ulValue /= 10;
    } while( ulValue != 0 );
    acBuf[usIdx] = '\0';

    for( usIdx--; usStart < usIdx; usStart++, usIdx-- )
    {
        cTmp = acBuf[usStart];
        acBuf[usStart] = acBuf[usIdx];
        acBuf[usIdx] = cTmp;
    }
    UcmdPrintf( acBuf );
}

/*----------------------------- 系统命令 -----------------------------------*/

/**
 * @brief 打印所有已注册命令的命令名与 C 原型(对应 "flist" 命令)
 * @param void
 * @return void
 */
void vUcmdGetFunList( void )
{
    uint16_t usIndex;

    for( usIndex = 0; xCmdFunList[usIndex].CmdFun != NULL; usIndex++ )
    {
        UcmdPrintf( xCmdFunList[usIndex].CmdName );
        UcmdPrintf( "  " );
        UcmdPrintf( xCmdFunList[usIndex].Prototype );
        UcmdPrintf( "\r\n" );
        UcmdPrintf( "\r\n" );
    }
}

/**
 * @brief 打印命令使用说明(对应 "help" 命令)
 * @param void
 * @return void
 */
void vUcmdGetHelp( void )
{
    UcmdPrintf("Help Usage:\n\
Commands must start with '>'. Separate the command and its parameters with spaces, \n\
and each parameter begin with '-'(Numeric-type parameters dont need to be prefixed with '-'.).\n\
Use the \"flist\" command to view all available commands.\n\n");
}

/* 系统命令也走统一入口: 套一层无参包装器后注册进 xCmdFunList */
UCMD_W0( vUcmdGetFunList )
UCMD_W0( vUcmdGetHelp )

/*-------------------------- 用户命令(示例) --------------------------------*/

/**
 * @brief echo 命令: 把字符串参数原样回显(前导 '-' 已被 UCMD_CV_s 剥掉)
 * @param pcStr 命令行传来的字符串参数
 * @return void
 */
void vCmdEcho( char *pcStr )
{
    UcmdPrintf( pcStr );
    UcmdPrintf( "\r\n" );
    UcmdPrintf( "\r\n" );
}
UCMD_W1( vCmdEcho, s )

/**
 * @brief led 命令: 参数 '1' 点亮 PF10(低电平点亮), 其他值熄灭
 * @param cOn 控制字符('1' 点亮, 其余熄灭)
 * @return void
 */
void vCmdLed( char cOn )
{
    HAL_GPIO_WritePin( GPIOF, GPIO_PIN_10,
                       ( cOn == '1' ) ? GPIO_PIN_RESET : GPIO_PIN_SET );
}
UCMD_W1( vCmdLed, c )

/**
 * @brief add 命令: 打印两个整数参数的和
 * @param a 第一个加数
 * @param b 第二个加数
 * @return void
 */
void vCmdAdd( int a, int b )
{
    vUcmdPrintDec( a + b );
    UcmdPrintf( "\r\n" );
    UcmdPrintf( "\r\n" );
}
UCMD_W2( vCmdAdd, i, i )

/**
 * @brief pwm 命令: 打印通道号与占空比(千分比), 演示 uint32_t + float 混合参数
 * @param uChannel PWM 通道号
 * @param fDuty 占空比(0.0 ~ 1.0)
 * @return void
 */
void vCmdPwm( uint32_t uChannel, float fDuty )
{
    vUcmdPrintDec( ( int32_t )uChannel );
    UcmdPrintf( " " );
    vUcmdPrintDec( ( int32_t )( fDuty * 1000.0f ) );
    UcmdPrintf( "/1000\r\n" );
    UcmdPrintf( "\r\n" );
}
UCMD_W2( vCmdPwm, u, f )

/**
 * @brief thermo 命令: 打印温度值(放大 100 倍), 演示 int + double 混合参数
 * @param iSensor 传感器编号(本示例未使用)
 * @param dTemp 温度值
 * @return void
 */
void vCmdThermo( int iSensor, double dTemp )
{
    ( void )iSensor;
    vUcmdPrintDec( ( int32_t )( dTemp * 100.0 ) );
    UcmdPrintf( "(x100)\r\n" );
    UcmdPrintf( "\r\n" );
}
UCMD_W2( vCmdThermo, i, d )

/*------------------------- 命令查找与行解析 -------------------------------*/

/**
 * @brief 在 xCmdFunList 中查找名字为 pcName(长度 usNameLen)的命令
 * @param pcName 待查找的命令名字符串指针
 * @param usNameLen pcName 的有效长度(不含结尾 '\0')
 * @return 查找到的表下标; 未找到返回 0xFFFF
 */
static uint16_t usUcmdFindCmd( const char *pcName, uint16_t usNameLen )
{
    uint16_t usIndex = 0;
    uint16_t usIdx;

    while( xCmdFunList[usIndex].CmdFun != NULL )
    {
        for( usIdx = 0;
             xCmdFunList[usIndex].CmdName[usIdx] != '\0' && usIdx < usNameLen;
             usIdx++ )
        {
            if( pcName[usIdx] != xCmdFunList[usIndex].CmdName[usIdx] )
            {
                break;
            }
        }
        if( xCmdFunList[usIndex].CmdName[usIdx] == '\0' && usIdx == usNameLen )
        {
            return usIndex;
        }
        usIndex++;
    }
    return 0xFFFF;
}

/**
 * @brief 把一条已解析命令拷入命令队列(队满由调用方保证)
 * @param pxCmd 指向待入队的命令结构
 * @return void
 */
static void ucUcmdQueuePush( const Cmd_t *pxCmd )
{
    uint16_t usTail = ( usCommandListIndex + usCommandListSize ) % configUCMD_CMDLIST_SIZE;

    xCommandList[usTail] = *pxCmd;
    usCommandListSize++;
}

/**
 * @brief 从 usCommandBufferIndex 起解析一条完整命令行
 *
 * 行格式: '>' + 命令名 + 若干空格分隔的参数, 行尾为 '\r'、'\n'、'\r\n'
 * 或缓冲区末尾。解析成功时填好 pxCmd(CmdIndex/Argc/Argv); 失败或没有
 * 完整数据时打印错误并丢弃整行返回 0。游标始终前进, 保证解析收敛。
 * @param pxCmd 输出: 指向填充好的命令结构(仅解析成功时有意义)
 * @return 1 = 解析成功; 0 = 该行无效或没有完整数据
 */
static uint8_t ucUcmdParseLine( Cmd_t *pxCmd )
{
    char *pc = ( char * )ucCommandBuffer;
    uint16_t usSize = usCommandBufferSize;
    uint16_t usIndex = usCommandBufferIndex;
    uint16_t usStart;
    uint16_t usNameLen;
    uint16_t usCmdIndex;
    uint8_t ucRet = 1;

    if( usIndex >= usSize )
    {
        usCommandBufferIndex = 0;
        usCommandBufferSize = 0;
        return 0;
    }

    /* 丢弃行前杂散数据直到 '>' */
    while( usIndex < usSize && pc[usIndex] != '>' )
    {
        usIndex++;
    }
    if( usIndex >= usSize )
    {
        usCommandBufferIndex = 0;
        usCommandBufferSize = 0;
        return 0;
    }
    usIndex++;   /* 越过 '>' */

    /* 命令名 */
    while( usIndex < usSize && ( pc[usIndex] == ' ' || pc[usIndex] == '\t' ) )
    {
        usIndex++;
    }
    if( usIndex >= usSize )   /* '>' 之后没有任何内容 */
    {
        usCommandBufferIndex = 0;
        usCommandBufferSize = 0;
        return 0;
    }
    usStart = usIndex;
    while( usIndex < usSize && pc[usIndex] != ' ' && pc[usIndex] != '\t' &&
           pc[usIndex] != '\r' && pc[usIndex] != '\n' )
    {
        usIndex++;
    }
    usNameLen = ( uint16_t )( usIndex - usStart );

    usCmdIndex = usUcmdFindCmd( &pc[usStart], usNameLen );
    if( usCmdIndex == 0xFFFF )
    {
        if( usNameLen != 0 )
        {
            UcmdPrintf( "unknown command: " );
            pc[usIndex] = '\0';
            UcmdPrintf( &pc[usStart] );
            UcmdPrintf( "\r\n" );
            UcmdPrintf( "\r\n" );
        }
        ucRet = 0;
        goto lineend;
    }

    /* 参数: 空格/制表符分隔, 原地补 '\0', Argv[] 保存文本指针 */
    pxCmd->CmdIndex = usCmdIndex;
    pxCmd->Argc = 0;
    while( usIndex < usSize )
    {
        while( usIndex < usSize && ( pc[usIndex] == ' ' || pc[usIndex] == '\t' ) )
        {
            usIndex++;
        }
        if( usIndex >= usSize || pc[usIndex] == '\r' || pc[usIndex] == '\n' )
        {
            break;
        }
        if( pxCmd->Argc >= UCMD_ARGV_MAX )
        {
            UcmdPrintf( "too many params\r\n" );
            UcmdPrintf( "\r\n" );
            ucRet = 0;
            goto lineend;
        }
        usStart = usIndex;

        if (pc[usIndex] == '"')
        {
            usIndex++;
            usStart++;
            while( usIndex < usSize && pc[usIndex] != '"' && pc[usIndex] != '\t' &&
               pc[usIndex] != '\r' && pc[usIndex] != '\n' )
            {
                usIndex++;
            }
        }
        else
        {
            while( usIndex < usSize && pc[usIndex] != ' ' && pc[usIndex] != '\t' &&
               pc[usIndex] != '\r' && pc[usIndex] != '\n' )
            {
                usIndex++;
            }
        }
        
        pc[usIndex] = '\0';
        usIndex++;
        pxCmd->Argv[pxCmd->Argc] = &pc[usStart];
        pxCmd->Argc++;
    }

lineend:
    /* 跳到下一行行首(允许 '\r'、'\n'、'\r\n' 三种行尾) */
    while( usIndex < usSize && pc[usIndex] != '\r' && pc[usIndex] != '\n' )
    {
        usIndex++;
    }
    if( usIndex < usSize && pc[usIndex] == '\r' )
    {
        usIndex++;
    }
    if( usIndex < usSize && pc[usIndex] == '\n' )
    {
        usIndex++;
    }
    if( usIndex >= usSize )
    {
        usCommandBufferIndex = 0;
        usCommandBufferSize = 0;
    }
    else
    {
        usCommandBufferIndex = usIndex;
    }
    return ucRet;
}

/*----------------------------- 公开接口 -----------------------------------*/

/**
 * @brief 把缓冲内完整的命令行解析入队; 队列满时暂停, 剩余数据留到下次调用
 * @param void
 * @return void
 */
void vUcmdCommandParsing( void )
{
    while( usCommandListSize < configUCMD_CMDLIST_SIZE )
    {
        Cmd_t xCmd;

        if( ucUcmdParseLine( &xCmd ) == 0 )
        {
            break;   /* 没有完整数据, 或该行无效(错误已打印) */
        }
        ucUcmdQueuePush( &xCmd );
    }
}

/**
 * @brief 从队列取出一条命令, 通过其包装器执行(参数个数校验在包装器内)
 * @param void
 * @return void
 */
void vUcmdUserCommandExecution( void )
{
    Cmd_t *pxCmd;
    CmdFun_t *pxEntry;

    if( usCommandListSize == 0 )
    {
        return;
    }

    pxCmd = &xCommandList[usCommandListIndex];
    pxEntry = &xCmdFunList[pxCmd->CmdIndex];
    pxEntry->CmdFun( pxCmd );

    usCommandListIndex = ( usCommandListIndex + 1 ) % configUCMD_CMDLIST_SIZE;
    usCommandListSize--;
}

/**
 * @brief 返回命令缓冲大小配置值
 * @param void
 * @return uint16_t 命令缓冲大小(configUCMD_CMDBUF_SIZE, 单位字节)
 */
uint16_t usUcmdReturn_configUCMD_CMDBUF_SIZE( void )
{
    return configUCMD_CMDBUF_SIZE;
}

/**
 * @brief 查询命令队列或解析缓冲中是否仍有待处理数据
 *
 * 供消费任务做"一次唤醒, 排空为止"的循环, 避免残留行被下次拉取覆盖。
 * @param void
 * @return uint8_t 1 = 仍有数据待处理; 0 = 已全部处理完
 */
uint8_t ucUcmdDataPending( void )
{
    return ( uint8_t )( ( usCommandListSize != 0 ) ||
                        ( usCommandBufferIndex < usCommandBufferSize ) );
}
