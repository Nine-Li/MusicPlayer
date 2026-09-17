/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "main.h"

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

/* Example: Declarations of the platform and disk functions in the project */
//#include "platform.h"
//#include "storage.h"

#include "ff_platform.h"
#include "sd2_driver.h"

/* Example: Mapping of physical drive number for each drive */
//#define DEV_FLASH	0	/* Map FTL to physical drive 0 */
//#define DEV_SD		1	/* Map MMC/SD card to physical drive 1 */
//#define DEV_USB		2	/* Map USB MSD to physical drive 2 */

#define DEV_SD		0
#define DEV_FLASH	1
#define DEV_USB		2



/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = STA_NOINIT;
	int result = 0;

	switch (pdrv) {
	case DEV_SD :
		result = SD_disk_status();

		// translate the reslut code here
		if (result == HAL_SD_CARD_TRANSFER) stat &= ~STA_NOINIT;

		return stat;

	case DEV_FLASH :
		result = FLASH_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_status();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = STA_NOINIT;
	int result = 0;

	switch (pdrv) {
	case DEV_SD :
		result = SD_disk_initialize();

		// translate the reslut code here
		if (result == 0) stat &= ~STA_NOINIT;

		return stat;

	case DEV_FLASH :
		result = FLASH_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_initialize();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res = RES_ERROR;
	int result;

	switch (pdrv) {
	case DEV_SD :
		// translate the arguments here

		result = SD_disk_read(buff, sector, count);

		// translate the reslut code here
		if (result == 0) res = RES_OK;

		return res;

	case DEV_FLASH :
		// translate the arguments here

		result = FLASH_disk_read(buff, sector, count);

		// translate the reslut code here
		if (result == 0) res = RES_OK;

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_ERROR;
	int result;

	switch (pdrv) {
	case DEV_SD :
		// translate the arguments here

		result = SD_disk_write(buff, sector, count);

		// translate the reslut code here
		if (result == 0) res = RES_OK;

		return res;

	case DEV_FLASH :
		// translate the arguments here

		result = FLASH_disk_write(buff, sector, count);

		// translate the reslut code here
		if (result == 0) res = RES_OK;

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res = RES_PARERR;

	switch (pdrv) {
	case DEV_SD :

		// Process of the command for the MMC/SD card
		switch (cmd) {
		case CTRL_SYNC :
			res = RES_OK;
			break;

		case GET_SECTOR_COUNT :
			*(LBA_t *)buff = sd2_handle.SdCard.LogBlockNbr;
			res = RES_OK;
			break;

		case GET_SECTOR_SIZE :
			*(WORD *)buff = sd2_handle.SdCard.LogBlockSize;
			res = RES_OK;
			break;

		case GET_BLOCK_SIZE :
			*(DWORD *)buff = sd2_handle.SdCard.LogBlockSize / SD_BLOCKSIZE;
			res = RES_OK;
			break;

		default :
			res = RES_PARERR;
			break;
		}

		return res;

	case DEV_FLASH :

		// Process of the command for the RAM drive
		if (cmd == CTRL_SYNC) res = RES_OK;

		return res;

	case DEV_USB :

		// Process of the command the USB drive
		if (cmd == CTRL_SYNC) res = RES_OK;

		return res;
	}

	return RES_PARERR;
}

