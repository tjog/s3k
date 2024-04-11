#include "../config.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "ff.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"

#define PROCESS_NAME "fs"

static FATFS FatFs; /* FatFs work area needed for each volume */

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

void print_cap(s3k_cap_t cap)
{
	switch (cap.type) {
	case S3K_CAPTY_TIME:
		alt_printf("ty=TIME, hart=%d, bgn=%d, mrk=%d, end=%d", cap.time.hart, cap.time.bgn,
			   cap.time.mrk, cap.time.end);
		break;
	case S3K_CAPTY_MEMORY:
		alt_printf("ty=MEMORY, rwx=%d, lck=%d, bgn=%d, mrk=%d, end=%d", cap.mem.rwx,
			   cap.mem.lck, cap.mem.bgn, cap.mem.mrk, cap.mem.end);
		break;
	case S3K_CAPTY_PMP:
		alt_printf("ty=PMP, rwx=%d, used=%d, slot=%d, addr=0x%X", cap.pmp.rwx, cap.pmp.used,
			   cap.pmp.slot, cap.pmp.addr);
		break;
	case S3K_CAPTY_MONITOR:
		alt_printf("ty=MONTOR, bgn=%d, mrk=%d, end=%d", cap.mon.bgn, cap.mon.mrk,
			   cap.mon.end);
		break;
	case S3K_CAPTY_CHANNEL:
		alt_printf("ty=CHANNEL, bgn=%d, mrk=%d, end=%d", cap.chan.bgn, cap.chan.mrk,
			   cap.chan.end);
		break;
	case S3K_CAPTY_SOCKET:
		alt_printf("ty=SOCKET, mode=0x%X, perm=0x%X, chan=%d, tag=%d", cap.sock.mode,
			   cap.sock.perm, cap.sock.chan, cap.sock.tag);
		break;
	case S3K_CAPTY_PATH:
		alt_printf("ty=PATH, file=%d, read=%d, write=%d, tag=%d", cap.path.file,
			   cap.path.read, cap.path.write, cap.path.tag);
		break;
	case S3K_CAPTY_NONE:
		alt_putstr("ty=NONE");
		break;
	}
}

void dump_caps_range(s3k_cidx_t start, s3k_cidx_t end)
{
	for (size_t i = start; i <= end; i++) {
		alt_printf(PROCESS_NAME ": %d: ", i);
		s3k_cap_t cap;
		s3k_err_t err = s3k_cap_read(i, &cap);
		if (!err) {
			print_cap(cap);
			if (cap.type == S3K_CAPTY_PATH) {
				char buf[50];
				s3k_err_t err = s3k_path_read(i, buf, 50);
				if (!err) {
					alt_putstr(" (='");
					alt_putstr(buf);
					alt_putstr("')");
				} else
					alt_printf(" (Error from s3k_path_read: 0x%X)", err);
			}
		} else {
			alt_putstr("NONE");
			alt_printf(" (Error from s3k_cap_read: 0x%X)", err);
		}
		alt_putchar('\n');
	}
}

s3k_cidx_t find_free_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err == S3K_ERR_EMPTY)
			return i;
	}
	return S3K_CAP_CNT;
}

s3k_cidx_t find_server_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_SOCKET && c.sock.chan == FS_CHANNEL) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

int main(void)
{
	s3k_sync_mem();

	fs_init();
	alt_puts("File server initialized");
	alt_puts("Hello from file server");
	// dump_caps_range(0, S3K_CAP_CNT - 1);
	/*
	Setup a loop receiving messages on our server socket.
	Respond with data.
	Protocol:
		- Client sendrecv()'s a message "INIT", along with a PMP capability
		  where the clients requests will return larger structures and buffer
		  data for reading and writing.
		- Client sends commands mapping to one of the operations above.
	*/

	// Find what cidx our server socket is
	s3k_cidx_t server_cidx = find_server_cidx();
	if (server_cidx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME ": error: could not find file server's socket\n");
		return -1;
	}
	// Find a free cidx to receive clients capabilities
	s3k_cidx_t free_cidx = find_free_cidx();
	if (free_cidx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME
			   ": error: could not find a free cidx for file server to receive on\n");
		return -1;
	}

	s3k_reg_write(S3K_REG_SERVTIME, 15000);

	while (true) {
		s3k_reply_t recv_msg = s3k_sock_recv(server_cidx, free_cidx);
		if (recv_msg.err) {
			alt_printf(PROCESS_NAME ": error: s3k_sock_recv returned error %x\n",
				   recv_msg.err);
			continue;
		}
		alt_printf(PROCESS_NAME ": received from tag=%d, data=[%d, %d, %d, %d]",
			   recv_msg.tag, recv_msg.data[0], recv_msg.data[1], recv_msg.data[2],
			   recv_msg.data[3]);
		if (recv_msg.cap.type != S3K_CAPTY_NONE) {
			alt_putstr(" cap = (");
			print_cap(recv_msg.cap);
			alt_putchar(')');
		}
		alt_putchar('\n');

		switch ((fs_client_ops)recv_msg.data[0]) {
		case fs_client_init: {
			s3k_msg_t response = {0};
			response.send_cap = false;
			response.data[0] = FS_ERR_INVALID_CAPABILITY;
			alt_printf(PROCESS_NAME ": sending, data=[%d, %d, %d, %d]\n",
				   response.data[0], response.data[1], response.data[2],
				   response.data[3]);
			s3k_err_t err = s3k_sock_send(server_cidx, &response);
			if (err) {
				alt_printf(
				    PROCESS_NAME ": error: s3k_sock_send returned error %d\n", err);
			} else {
				alt_printf(PROCESS_NAME ": s3k_sock_send succeeded\n");
			}
		} break;

		default: {
			s3k_msg_t response = {0};
			response.data[0] = FS_ERR_INVALID_OPERATION_CODE;
			s3k_err_t err = s3k_sock_send(server_cidx, &response);
			if (err) {
				alt_printf(
				    PROCESS_NAME ": error: s3k_sock_send returned error %d\n", err);
			}
		} break;
		}
	}
}
