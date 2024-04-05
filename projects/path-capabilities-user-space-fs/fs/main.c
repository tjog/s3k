#include "altc/altio.h"
#include "altc/string.h"
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
#define ROOT_PATH 11

// Derived
#define UART_PMP 12
#define UART_PMP_SLOT 1
#define VIRTIO_PMP 13
#define VIRTIO_PMP_SLOT 2

static FATFS FatFs; /* FatFs work area needed for each volume */

typedef enum {
	FS_SUCCESS,
	FS_ERR_FILE_OPEN,
	FS_ERR_FILE_SEEK,
	FS_ERR_FILE_READ,
	FS_ERR_FILE_WRITE,
	FS_ERR_PATH_EXISTS,
	FS_ERR_PATH_STAT,
	FS_ERR_INVALID_INDEX,
} fs_err_t;

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

void fs_init()
{
	FRESULT fr;
	fr = f_mount(&FatFs, "", 0); /* Give a work area to the default drive */
	if (fr == FR_OK) {
		alt_puts("File system mounted OK");
	} else {
		alt_printf("File system not mounted: %s\n", fresult_get_error(fr));
	}
}

fs_err_t read_file(char *path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		   uint32_t *bytes_read)
{
	FIL Fil; /* File object needed for each open file */
	FRESULT fr;
	fs_err_t err = FS_SUCCESS;

	fr = f_open(&Fil, path, FA_READ);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_OPEN;
		goto ret;
	}
	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_SEEK;
		goto cleanup;
	}
	fr = f_read(&Fil, buf, buf_size, bytes_read);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_READ;
		goto cleanup;
	}
cleanup:
	f_close(&Fil);
ret:
	return err;
}

fs_err_t read_dir(char *path, size_t dir_entry_idx, s3k_dir_entry_info_t *out)
{
	FILINFO fi;
	DIR di;
	fs_err_t err = FS_SUCCESS;
	FRESULT fr = f_opendir(&di, path);
	if (fr != FR_OK) {
		err = FS_ERR_FILE_OPEN;
		goto out;
	}
	for (size_t i = 0; i <= dir_entry_idx; i++) {
		fr = f_readdir(&di, &fi);
		if (fr != FR_OK) {
			err = FS_ERR_FILE_SEEK;
			goto cleanup;
		}
		// End of directory
		if (fi.fname[0] == 0) {
			err = FS_ERR_INVALID_INDEX;
			goto cleanup;
		}
	}
	// Could do one larger memcpy here, but not certain FatFS file info and S3K
	// file info will continue to stay in sync, so leverage the type safety of
	// explicit structure assignment.
	out->fattrib = fi.fattrib;
	out->fdate = fi.fdate;
	out->fsize = fi.fsize;
	out->ftime = fi.ftime;
	memcpy(out->fname, fi.fname, sizeof(fi.fname));
cleanup:
	f_closedir(&di);
out:
	return err;
}

fs_err_t create_dir(char *path, bool ensure_create)
{
	FRESULT fr = f_mkdir(path);
	if (fr == FR_EXIST) {
		if (ensure_create)
			return FS_ERR_PATH_EXISTS;
		// Check that the existing entry is a dir
		FILINFO fno;
		fr = f_stat(path, &fno);
		if (fr != FR_OK) {
			return FS_ERR_PATH_STAT;
		}
		if (fno.fattrib & AM_DIR)
			return FS_SUCCESS;
		// Exists as file, not what we want
		return FS_ERR_PATH_EXISTS;
	} else if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		return FS_ERR_FILE_WRITE;
	}
	return FS_SUCCESS;
}

fs_err_t write_file(char *path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		    uint32_t *bytes_written)
{
	FIL Fil; /* File object needed for each open file */
	FRESULT fr;
	fs_err_t err = FS_SUCCESS;

	// FA_OPEN_ALWAYS means open the existing file or create it, i.e. succeed in both cases
	fr = f_open(&Fil, path, FA_WRITE | FA_OPEN_ALWAYS);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_OPEN;
		goto ret;
	}
	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_SEEK;
		goto cleanup;
	}
	fr = f_write(&Fil, buf, buf_size, bytes_written);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = FS_ERR_FILE_READ;
		goto cleanup;
	}
cleanup:
	f_close(&Fil);
ret:
	return err;
}

fs_err_t path_delete(char *path)
{
	FRESULT fr = f_unlink(path);
	if (fr == FR_DENIED) {
		// Not empty, is current directory, or read-only attribute
		return FS_ERR_PATH_EXISTS;
	} else if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		return FS_ERR_FILE_WRITE;
	}
	return FS_SUCCESS;
}

fs_err_t setup_pmp_from_mem_cap(s3k_cidx_t mem_cap_idx, s3k_cidx_t pmp_cap_idx,
				s3k_pmp_slot_t pmp_slot, s3k_napot_t napot_addr, s3k_rwx_t rwx)
{
	fs_err_t err = FS_SUCCESS;
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
	/*
	Setup a loop receiving messages on our server socket.
	Respond with data.
	Protocol:
		- Client sendrecv()'s a message "INIT", along with a PMP capability
		  where the clients requests will return larger structures and buffer
		  data for reading and writing.
		- Client sends commands mapping to one of the operations above.
	*/
}
