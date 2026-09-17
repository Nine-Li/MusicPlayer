#ifndef __APPTASK_H
#define __APPTASK_H

#include "main.h"

typedef enum
{
    Music_Play = 1,
    Music_Pause,
    Music_Resume,
    Music_Stop,
}Music_Daemon_Cmd_t;

typedef struct 
{
    char Path[255];
    size_t Offset;
    Music_Daemon_Cmd_t Music_Daemon_Cmd;
}Music_Daemon_Reg_t;

void vInitTask( void *param);
void vUcmd_Daemon(void *vpParams);
void vMusic_Daemon( void *param );
void vSD_GateKeeper( void *param);
void vMusic_Player( void *param);

#endif
