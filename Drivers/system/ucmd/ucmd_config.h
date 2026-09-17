#ifndef __UCMD_CONF_H
#define __UCMD_CONF_H

#include "ucmd.h"   /* 需要 CmdFun_t / UCMD_DECL */

#define configUCMD_CMDBUF_SIZE 128u  //单位字节,最大值65535。缓冲数组实际为 SIZE+1, 预留原位补 '\0' 的位置。
#define configUCMD_CMDLIST_SIZE 1u   //单位为结构体Cmd_t,最大值65535。命令行解析入队的最大条数。

/* 命令注册方法:
   1. 在 ucmd.c(或任意 TU)中编写真实函数并紧跟一行 UCMD_Wn(fn, 类型字母...),
      生成包装器 fn_Wrap; 包装器内做参数个数校验和文本->类型转换。
   2. 在下方加入一行 UCMD_DECL(fn) 声明包装器;
   3. 在 xCmdFunList[] 中加入 { 命令名, fn_Wrap, "真实函数的C原型" }。
   类型字母: c=char  s=char*字符串  i=int  u=uint32_t  f=float  d=double
   xCmdFunList 是全局定义(以 {0} 结尾), 本文件只能被一个 .c 包含, 否则链接重复。 */

UCMD_DECL( vUcmdGetHelp )
UCMD_DECL( vUcmdGetFunList )
UCMD_DECL( vCmdEcho )
UCMD_DECL( vCmdLed )
UCMD_DECL( vCmdAdd )
UCMD_DECL( vCmdPwm )
UCMD_DECL( vCmdThermo )
UCMD_DECL( open_file )
UCMD_DECL( list_curdir )
UCMD_DECL( vMusic_Play )
UCMD_DECL( vMusic_Pause )
UCMD_DECL( vMusic_Resume )
UCMD_DECL( vMusic_Stop )

CmdFun_t xCmdFunList[] =
{
    {"help",      vUcmdGetHelp_Wrap,      "void vUcmdGetHelp( void )"},
    {"flist",     vUcmdGetFunList_Wrap,   "void vUcmdGetFunList( void )"},
    {"echo",      vCmdEcho_Wrap,          "void vCmdEcho( char *pcStr )"},
    {"led",       vCmdLed_Wrap,           "void vCmdLed( char cOn )"},
    {"add",       vCmdAdd_Wrap,           "void vCmdAdd( int a, int b )"},
    {"pwm",       vCmdPwm_Wrap,           "void vCmdPwm( uint32_t uChannel, float fDuty )"},
    {"thermo",    vCmdThermo_Wrap,        "void vCmdThermo( int iSensor, double dTemp )"},
    {"open",      open_file_Wrap,         "void open_file( uint8_t *Path )"},
    {"ls",        list_curdir_Wrap,       "void list_curdir ( void )"},
    {"plm",       vMusic_Play_Wrap,       "void vMusic_Play( char * Path )"},
    {"pam",       vMusic_Pause_Wrap,      "void vMusic_Pause( void )"},
    {"rem",       vMusic_Resume_Wrap,     "void vMusic_Resume( void )"},
    {"stm",       vMusic_Stop_Wrap,       "void vMusic_Stop( void )"},
    {0} //用于标识列表结尾
};

#endif
