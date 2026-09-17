#ifndef __UCMD_H
#define __UCMD_H

#include "main.h"
#include <stdlib.h>   /* strtol/strtoul/strtod, 供 UCMD_CV_* 转换宏使用 */

/* 串口接收层(尚未实现)往 ucCommandBuffer 里写入完整命令行并把字节数写入
   usCommandBufferSize, 之后调用 vUcmdCommandParsing()/vUcmdUserCommandExecution()。
   行格式: '>'命令名[空格参数]... 以 '\r' 或 '\n' 或缓冲末尾结束。
   参数解析采用"文本指针"方式: 解析器在缓冲区内原位把参数切开补 '\0',
   Argv[] 只保存指针, 不复制数据; 类型转换由每条命令自己的包装器完成。 */

#define UCMD_ARGV_MAX 10u   /* 单条命令最多参数个数, 与 Cmd_t::Argv 对应 */

typedef struct
{
    uint16_t CmdIndex;            /* 在 xCmdFunList[] 中的下标 */
    uint16_t Argc;                /* 实际参数个数 */
    char *Argv[UCMD_ARGV_MAX];    /* 参数文本指针(缓冲区内 '\0' 结尾) */
} Cmd_t;

/* 所有命令经包装器以该统一签名被执行, 由宏生成的包装器负责把文本参数按声明的
   类型转换后, 用与目标函数原型完全匹配的函数指针类型转调真实函数。这样
   float/double 参数走 VFP 寄存器(s0-s3/d0-d3)的 ABI 规则由编译器保证, 不存在
   运行时"把参数拼进整数寄存器"导致的取参错误。 */
typedef void ( *CmdHandler_t )( Cmd_t *pxCmd );

typedef struct
{
    char *CmdName;       /* 串口命令名(不含 '>' 前缀) */
    CmdHandler_t CmdFun; /* 必须指向 UCMD_Wn() 生成的包装器 */
    char *Prototype;     /* 人类可读的 C 原型文本, "flist" 打印用 */
} CmdFun_t;

extern CmdFun_t xCmdFunList[];  /* 定义于 ucmd_config.h, 该文件只能被一个 .c 包含 */

/* 移植函数与公开接口 (实现在 ucmd.c) */
void UcmdPrintf( char *Str );            /* 打印一个以 '\0' 结尾的字符串, 移植点 */
void vUcmdCommandParsing( void );        /* 把缓冲内的完整命令行解析入队, 队满即停 */
void vUcmdUserCommandExecution( void );  /* 从队列取出一条命令并调用其包装器 */
uint8_t ucUcmdDataPending( void );       /* 1=队列/缓冲中仍有待解析执行的数据 */
uint16_t usUcmdReturn_configUCMD_CMDBUF_SIZE( void );

/* 接收层写入接口 */
extern uint8_t ucCommandBuffer[];
extern uint16_t usCommandBufferSize;

/*----------------------------------------------------------------------------*/
/* UCMD_Wn() 用法: 命令按普通函数编写, 用对应参数个数的宏生成包装器。例:
 *
 *     void vCmdAdd( int a, int b ) { ... }
 *     UCMD_W2( vCmdAdd, i, i )   // 生成: void vCmdAdd_Wrap( Cmd_t *pxCmd );
 *
 * 然后把包装器注册进 xCmdFunList:
 *     { "add", vCmdAdd_Wrap, "void vCmdAdd( int a, int b )" }
 *
 * 类型字母(按顺序对应每个参数): c=char, s=char* 字符串, i=int,
 * u=uint32_t, f=float, d=double。字符串/字符参数的前导 '-' 会被剥掉
 * (命令行里 '-' 是参数标记), 数字参数原样转换(负号保留, 如 "-12" -> -12)。
 * 参数个数不符时包装器打印错误并放弃执行该命令。
 *----------------------------------------------------------------------------*/

#define UCMD_WNAME( fn ) fn##_Wrap
#define UCMD_DECL( fn ) extern void UCMD_WNAME( fn )( Cmd_t *pxCmd );

#define UCMD_STRIP( p ) ( ( p )[0] == '-' ? ( p ) + 1 : ( p ) )

#define UCMD_CV_c( p ) ( UCMD_STRIP( p )[0] )
#define UCMD_CV_s( p ) ( UCMD_STRIP( p ) )
#define UCMD_CV_i( p ) ( ( int )strtol( ( p ), ( char ** )0, 0 ) )
#define UCMD_CV_u( p ) ( ( uint32_t )strtoul( ( p ), ( char ** )0, 0 ) )
#define UCMD_CV_f( p ) ( ( float )strtod( ( p ), ( char ** )0 ) )
#define UCMD_CV_d( p ) ( strtod( ( p ), ( char ** )0 ) )

#define UCMD_ARGCHK( pxCmd, n ) \
    if( ( pxCmd )->Argc != ( n ) ) \
    { \
        UcmdPrintf( "arg count mismatch\r\n" ); \
        return; \
    }

#define UCMD_W0( fn ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 0 ) \
        fn(); \
    }

#define UCMD_W1( fn, t0 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 1 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ) ); \
    }

#define UCMD_W2( fn, t0, t1 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 2 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ) ); \
    }

#define UCMD_W3( fn, t0, t1, t2 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 3 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ) ); \
    }

#define UCMD_W4( fn, t0, t1, t2, t3 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 4 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ) ); \
    }

#define UCMD_W5( fn, t0, t1, t2, t3, t4 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 5 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ) ); \
    }

#define UCMD_W6( fn, t0, t1, t2, t3, t4, t5 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 6 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ), UCMD_CV_##t5( ( pxCmd )->Argv[5] ) ); \
    }

#define UCMD_W7( fn, t0, t1, t2, t3, t4, t5, t6 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 7 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ), UCMD_CV_##t5( ( pxCmd )->Argv[5] ), \
            UCMD_CV_##t6( ( pxCmd )->Argv[6] ) ); \
    }

#define UCMD_W8( fn, t0, t1, t2, t3, t4, t5, t6, t7 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 8 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ), UCMD_CV_##t5( ( pxCmd )->Argv[5] ), \
            UCMD_CV_##t6( ( pxCmd )->Argv[6] ), UCMD_CV_##t7( ( pxCmd )->Argv[7] ) ); \
    }

#define UCMD_W9( fn, t0, t1, t2, t3, t4, t5, t6, t7, t8 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 9 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ), UCMD_CV_##t5( ( pxCmd )->Argv[5] ), \
            UCMD_CV_##t6( ( pxCmd )->Argv[6] ), UCMD_CV_##t7( ( pxCmd )->Argv[7] ), \
            UCMD_CV_##t8( ( pxCmd )->Argv[8] ) ); \
    }

#define UCMD_W10( fn, t0, t1, t2, t3, t4, t5, t6, t7, t8, t9 ) \
    void UCMD_WNAME( fn )( Cmd_t *pxCmd ) \
    { \
        UCMD_ARGCHK( pxCmd, 10 ) \
        fn( UCMD_CV_##t0( ( pxCmd )->Argv[0] ), UCMD_CV_##t1( ( pxCmd )->Argv[1] ), \
            UCMD_CV_##t2( ( pxCmd )->Argv[2] ), UCMD_CV_##t3( ( pxCmd )->Argv[3] ), \
            UCMD_CV_##t4( ( pxCmd )->Argv[4] ), UCMD_CV_##t5( ( pxCmd )->Argv[5] ), \
            UCMD_CV_##t6( ( pxCmd )->Argv[6] ), UCMD_CV_##t7( ( pxCmd )->Argv[7] ), \
            UCMD_CV_##t8( ( pxCmd )->Argv[8] ), UCMD_CV_##t9( ( pxCmd )->Argv[9] ) ); \
    }

#endif
