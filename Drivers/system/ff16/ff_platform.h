#ifndef __FF_PLATFORM_H
#define __FF_PLATFORM_H

#include "ff.h"

#define SD_BLOCKSIZE 512u

int FLASH_disk_status(void);
int SD_disk_status(void);
int USB_disk_status(void);

int FLASH_disk_initialize(void);
int SD_disk_initialize(void);
int USB_disk_initialize(void);

int FLASH_disk_read(BYTE *buff, LBA_t sector, UINT count);
int SD_disk_read(BYTE *buff, LBA_t sector, UINT count);
int USB_disk_read(BYTE *buff, LBA_t sector, UINT count);

int FLASH_disk_write(const BYTE *buff, LBA_t sector, UINT count);
int SD_disk_write(const BYTE *buff, LBA_t sector, UINT count);
int USB_disk_write(const BYTE *buff, LBA_t sector, UINT count);

void vSD_GateKeeper( void *param);

#endif
