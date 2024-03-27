#include "cap_ops.h"
#include "cap_table.h"
#include "cap_util.h"
#include "csr.h"
#include "drivers/time.h"
#include "error.h"
#include "ff.h"
#include "kassert.h"
#include "proc.h"

#include <stdint.h>

#define MAX_PATH 100
#define MAX_TAGS 10

typedef struct {
	bool occupied;
	char path[MAX_PATH];
} stored_path;

static int current_tag = 0;
static stored_path stored_paths[MAX_TAGS] = {
    [0] = {
	   .occupied = true,
	   .path = "/",
	   }
};
FATFS FatFs; /* FatFs work area needed for each volume */

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
		alt_putstr("File system not mounted: ");
		alt_putstr(fresult_get_error(fr));
		alt_putchar('\n');
	}

	// UINT bw;
	// FIL Fil; /* File object needed for each open file */
	// fr = f_open(&Fil, "newfile.txt", FA_WRITE | FA_CREATE_ALWAYS); /* Create a file */
	// if (fr == FR_OK) {
	// 	alt_puts("File opened for writing");
	// 	f_write(&Fil, "It works!\r\n", 11, &bw); /* Write data to the file */
	// 	fr = f_close(&Fil);			 /* Close the file */
	// 	if (fr == FR_OK && bw == 11) {
	// 		alt_puts("File saved");
	// 	} else {
	// 		alt_putstr("File not saved: ");
	// 		alt_putstr(fresult_get_error(fr));
	// 		alt_putchar('\n');
	// 	}
	// } else {
	// 	alt_putstr("File not opened: ");
	// 	alt_putstr(fresult_get_error(fr));
	// 	alt_putchar('\n');
	// }

	// char buffer[1024];
	// fr = f_open(&Fil, "test.txt", FA_READ);
	// if (fr == FR_OK) {
	// 	alt_puts("File opened for reading");
	// 	f_read(&Fil, buffer, 1023, &bw); /*Read data from the file */
	// 	fr = f_close(&Fil);		 /* Close the file */
	// 	if (fr == FR_OK) {
	// 		buffer[bw] = '\0';
	// 		alt_puts(buffer);
	// 	} else {
	// 		alt_putstr("File not read: ");
	// 		alt_putstr(fresult_get_error(fr));
	// 		alt_putchar('\n');
	// 	}
	// } else {
	// 	alt_putstr("File not opened: ");
	// 	alt_putstr(fresult_get_error(fr));
	// 	alt_putchar('\n');
	// }
}

int find_next_free_tag()
{
	int idx = current_tag;
	while (stored_paths[idx].occupied) {
		idx = (idx + 1) % MAX_TAGS;
		// If we have gone a full cycle without finding a free tag
		if (idx == current_tag)
			return -1;
	}
	return idx;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	// Copy characters from src to dest until n characters are copied or null terminator is reached
	size_t i = 0;
	while (i < n && src[i] != '\0') {
		dest[i] = src[i];
		i++;
	}

	// Pad the remaining characters in dest with null terminators
	while (i < n) {
		dest[i] = '\0';
		i++;
	}

	// Return the destination pointer
	return dest;
}

err_t path_derive(cte_t src, cte_t dst, const char *path, path_flags_t flags)
{
	if (!cte_cap(src).type)
		return ERR_SRC_EMPTY;

	if (cte_cap(dst).type)
		return ERR_DST_OCCUPIED;
	cap_t scap = cte_cap(src);

	if (scap.type != CAPTY_PATH)
		return ERR_INVALID_DERIVATION;

	// Can only derive file to file when not using a new path i.e NULL pointer (0)
	if (scap.path.file && path != NULL)
		return ERR_INVALID_DERIVATION;

	int next_tag = find_next_free_tag();
	if (next_tag == -1) {
		return ERR_NO_PATH_TAG;
	}
	stored_paths[next_tag].occupied = true;
	strncpy(stored_paths[next_tag].path, path, MAX_PATH);
	cap_t ncap = cap_mk_path(next_tag, flags);
	current_tag = next_tag;

	cte_insert(dst, ncap, src);
	cte_set_cap(src, scap);
	return SUCCESS;
}

err_t read_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size, uint32_t *bytes_read)
{
	FIL Fil; /* File object needed for each open file */
	FRESULT fr;

	fr = f_open(&Fil, stored_paths[path.path.tag].path, FA_READ);
	if (fr != FR_OK) {
		return ERR_FILE_OPEN;
	}
	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		return ERR_FILE_SEEK;
	}
	fr = f_read(&Fil, buf, buf_size, bytes_read);
	if (fr != FR_OK) {
		return ERR_FILE_READ;
	}
	return SUCCESS;
}

err_t write_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		 uint32_t *bytes_written)
{
	FIL Fil; /* File object needed for each open file */
	FRESULT fr;

	fr = f_open(&Fil, stored_paths[path.path.tag].path, FA_WRITE | FA_CREATE_ALWAYS);
	if (fr != FR_OK) {
		return ERR_FILE_OPEN;
	}
	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		return ERR_FILE_SEEK;
	}
	fr = f_write(&Fil, buf, buf_size, bytes_written);
	if (fr != FR_OK) {
		return ERR_FILE_READ;
	}
	return SUCCESS;
}
