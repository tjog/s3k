#include "altc/altio.h"
#include "ff.h"
#include "s3k/s3k.h"

#define APP0_PID 0
#define APP1_PID 1

// See plat_conf.h
#define BOOT_PMP 0
#define RAM_MEM 1
#define UART_MEM 2
#define TIME_MEM 3
#define HART0_TIME 4
#define HART1_TIME 5
#define HART2_TIME 6
#define HART3_TIME 7
#define MONITOR 8
#define CHANNEL 9
#define VIRTIO_MEM 10

// Derived
#define UART_PMP 11
#define UART_PMP_SLOT 1
#define VIRTIO_PMP 12
#define VIRTIO_PMP_SLOT 2

char *fresult_get_error(FRESULT fr)
{
	switch (fr) {
	case FR_OK:
		return "(0) Succeeded";
	case FR_DISK_ERR:
		return "(1) A hard error occurred in the low level disk I/O layer";
	case FR_INT_ERR:
		return "(2) Assertion failed";
	case FR_NOT_READY:
		return "(3) The physical drive cannot work";
	case FR_NO_FILE:
		return "(4) Could not find the file";
	case FR_NO_PATH:
		return "(5) Could not find the path";
	case FR_INVALID_NAME:
		return "(6) The path name format is invalid";
	case FR_DENIED:
		return "(7) Access denied due to prohibited access or directory full";
	case FR_EXIST:
		return "(8) Access denied due to prohibited access";
	case FR_INVALID_OBJECT:
		return "(9) The file/directory object is invalid";
	case FR_WRITE_PROTECTED:
		return "(10) The physical drive is write protected";
	case FR_INVALID_DRIVE:
		return "(11) The logical drive number is invalid";
	case FR_NOT_ENABLED:
		return "(12) The volume has no work area";
	case FR_NO_FILESYSTEM:
		return "(13) There is no valid FAT volume";
	case FR_MKFS_ABORTED:
		return "(14) The f_mkfs() aborted due to any problem";
	case FR_TIMEOUT:
		return "(15) Could not get a grant to access the volume within defined period";
	case FR_LOCKED:
		return "(16) The operation is rejected according to the file sharing policy";
	case FR_NOT_ENOUGH_CORE:
		return "(17) LFN working buffer could not be allocated";
	case FR_TOO_MANY_OPEN_FILES:
		return "(18) Number of open files > FF_FS_LOCK";
	case FR_INVALID_PARAMETER:
		return "(19) Given parameter is invalid";
	default:
		return "(XX) Unknown";
	}
}

s3k_err_t setup_pmp_from_mem_cap(s3k_cidx_t mem_cap_idx, s3k_cidx_t pmp_cap_idx,
				 s3k_pmp_slot_t pmp_slot, s3k_napot_t napot_addr, s3k_rwx_t rwx)
{
	s3k_err_t err = S3K_SUCCESS;
	err = s3k_cap_derive(mem_cap_idx, pmp_cap_idx, s3k_mk_pmp(napot_addr, rwx));
	if (err)
		return err;
	err = s3k_pmp_load(pmp_cap_idx, pmp_slot);
	if (err)
		return err;
	s3k_sync_mem();
	return err;
}

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_napot_t virtio_addr = s3k_napot_encode(VIRTIO0_BASE_ADDR, 0x1000);

	s3k_err_t err
	    = setup_pmp_from_mem_cap(UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	err = setup_pmp_from_mem_cap(VIRTIO_MEM, VIRTIO_PMP, VIRTIO_PMP_SLOT, virtio_addr,
				     S3K_MEM_RW);
	if (err)
		alt_printf("Virtio setup error code: %x\n", err);
	alt_puts("finished setting up virtio");

	FATFS FatFs; /* FatFs work area needed for each volume */
	FRESULT fr;
	fr = f_mount(&FatFs, "", 0); /* Give a work area to the default drive */
	if (fr == FR_OK) {
		alt_puts("File system mounted OK");
	} else {
		alt_putstr("File system not mounted: ");
		alt_putstr(fresult_get_error(fr));
		alt_putchar('\n');
	}

	UINT bw;
	FIL Fil; /* File object needed for each open file */
	fr = f_open(&Fil, "newfile.txt", FA_WRITE | FA_CREATE_ALWAYS); /* Create a file */
	if (fr == FR_OK) {
		alt_puts("File opened for writing");
		f_write(&Fil, "It works!\r\n", 11, &bw); /* Write data to the file */
		fr = f_close(&Fil);			 /* Close the file */
		if (fr == FR_OK && bw == 11) {
			alt_puts("File saved");
		} else {
			alt_putstr("File not saved: ");
			alt_putstr(fresult_get_error(fr));
			alt_putchar('\n');
		}
	} else {
		alt_putstr("File not opened: ");
		alt_putstr(fresult_get_error(fr));
		alt_putchar('\n');
	}

	char buffer[1024];
	fr = f_open(&Fil, "test.txt", FA_READ);
	if (fr == FR_OK) {
		alt_puts("File opened for reading");
		f_read(&Fil, buffer, 1023, &bw); /*Read data from the file */
		fr = f_close(&Fil);		 /* Close the file */
		if (fr == FR_OK) {
			buffer[bw] = '\0';
			alt_puts(buffer);
		} else {
			alt_putstr("File not read: ");
			alt_putstr(fresult_get_error(fr));
			alt_putchar('\n');
		}
	} else {
		alt_putstr("File not opened: ");
		alt_putstr(fresult_get_error(fr));
		alt_putchar('\n');
	}
}
