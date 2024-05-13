/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h" /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */
#include <string.h>

/* Definitions of physical drive number for each drive */
#define DEV_INMEM 0 /* Example: Map Inmemdisk to physical drive 0 */

#define SECTOR_SIZE 512 /* Actually a virtual sector size */
// uint8_t in_mem_disk[2048 * SECTOR_SIZE]; /* 1 MB */
uint8_t in_mem_disk[1700 * SECTOR_SIZE]; /* 1 MB */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
	switch (pdrv) {
	case DEV_INMEM:
		return 0;
	}
	return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS
disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
	switch (pdrv) {
	case DEV_INMEM:
		return disk_status(pdrv);
	}

	return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE pdrv,  /* Physical drive nmuber to identify the drive */
		  BYTE *buff, /* Data buffer to store read data */
		  LBA_t sector, /* Start sector in LBA */
		  UINT count	/* Number of sectors to read */
)
{
	switch (pdrv) {
	case DEV_INMEM:
		// translate the arguments here
		if (disk_status(pdrv) & STA_NOINIT)
			return RES_NOTRDY;

		memcpy(buff, &in_mem_disk[sector * SECTOR_SIZE],
		       SECTOR_SIZE * count);
		return RES_OK;
	}

	return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, /* Physical drive nmuber to identify the drive */
		   const BYTE *buff, /* Data to be written */
		   LBA_t sector,     /* Start sector in LBA */
		   UINT count	     /* Number of sectors to write */
)
{
	switch (pdrv) {
	case DEV_INMEM:
		// translate the arguments here
		if (disk_status(pdrv) & STA_NOINIT)
			return RES_NOTRDY;

		memcpy(&in_mem_disk[sector * SECTOR_SIZE], buff,
		       SECTOR_SIZE * count);
		return RES_OK;
	}

	return RES_PARERR;
}

#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE drv,  /* Physical drive nmuber (0) */
		   BYTE ctrl, /* Control code */
		   void *buff /* Buffer to send/receive control data */
)
{
	DRESULT res;

	if (disk_status(drv) & STA_NOINIT)
		return RES_NOTRDY; /* Check if card is in the socket */

	res = RES_ERROR;
	switch (ctrl) {
	case CTRL_SYNC: /* Make sure that no pending write process */
		return RES_OK;
		break;

	case GET_SECTOR_SIZE: /* Get number of sectors on the disk (DWORD) */
		*(DWORD *)buff = SECTOR_SIZE;
		return RES_OK;
		break;

	case GET_SECTOR_COUNT: /* Get number of sectors on the disk (DWORD) */
		*(LBA_t *)buff = sizeof(in_mem_disk) / SECTOR_SIZE;
		return RES_OK;
		break;

	case GET_BLOCK_SIZE: /* Get erase block size in unit of sector (DWORD) */
		*(DWORD *)buff = 1;
		res = RES_OK;
		break;

	default:
		res = RES_PARERR;
	}

	return res;
}

DWORD get_fattime(void)
{
	return 0;
}
