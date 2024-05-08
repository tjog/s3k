#include "../config.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "ff.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"

#if !FS_DEBUG
#define alt_printf(...)
#define alt_puts(...)
#define alt_putstr(...)
#define alt_putchar(...)
#define alt_puts(...)
#define print_cap(...)
#endif

#define PROCESS_NAME "fs"
#define MAX_CLIENTS 5

typedef struct client {
	uint32_t socket_tag;
	s3k_cidx_t pmp_cap_idx;
	bool active;
} client_t;

static FATFS FatFs; /* FatFs work area needed for each volume */
static client_t clients[MAX_CLIENTS];

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
	BYTE work[FF_MAX_SS];
	fr = f_mkfs("", 0, work, sizeof work);
	if (fr == FR_OK) {
		alt_puts("In mem disk formatting OK");
	} else {
		alt_printf("In mem disk not formatted: %s\n",
			   fresult_get_error(fr));
		return;
	}
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

/* Returns true if a is a bitwise subset of b */
static inline bool is_bit_subset(uint64_t a, uint64_t b)
{
	return (a & b) == a;
}

/* Returns true if the range b is a subset of range a */
static inline bool is_range_subset(uint64_t a_bgn, uint64_t a_end, uint64_t b_bgn, uint64_t b_end)
{
	return a_bgn <= b_bgn && b_end <= a_end;
}

/* Returns true if pmp_cidx is valid, buf+len is a memory range inside the pmpaddr, and the rwx requirement mask is met*/
static inline bool check_pmp(s3k_cidx_t pmp_cidx, uint8_t *buf, uint32_t len, s3k_rwx_t rwx_mask)
{
	s3k_cap_t cap;
	if (S3K_SUCCESS != s3k_cap_read(pmp_cidx, &cap))
		return false;
	if (cap.type != S3K_CAPTY_PMP)
		return false;
	if (!is_bit_subset(rwx_mask, cap.pmp.rwx))
		return false;
	s3k_addr_t pmp_base, pmp_len;
	s3k_napot_decode(cap.pmp.addr, &pmp_base, &pmp_len);
	if (!is_range_subset(pmp_base, pmp_base + pmp_len, (uint64_t)buf, (uint64_t)buf + len))
		return false;
	if (!cap.pmp.used) {
		s3k_err_t err = s3k_pmp_load(
		    pmp_cidx,
		    3); // TODO: maybe a routine or LRU approach to PMP slots to allow more efficient serving of clients
		if (err == S3K_ERR_DST_OCCUPIED) {
			err = s3k_pmp_unload(3);
			if (err)
				return false;
			err = s3k_pmp_load(
			    pmp_cidx,
			    3); // TODO: maybe a routine or LRU approach to PMP slots to allow more efficient serving of clients
			if (err)
				return false;
		}
		s3k_sync_mem();
	}
	return true;
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

size_t find_free_client_idx()
{
	for (size_t i = 0; i < MAX_CLIENTS; i++) {
		if (!clients[i].active)
			return i;
	}
	return MAX_CLIENTS;
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

s3k_msg_t do_fs_client_init(s3k_reply_t recv_msg, s3k_cidx_t receive_cidx)
{
	s3k_msg_t response = {0};
	response.send_cap = false;
	if (recv_msg.cap.type != S3K_CAPTY_PMP) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
	} else {
		size_t client_num = find_free_client_idx();
		if (client_num == MAX_CLIENTS) {
			response.data[0] = FS_ERR_MAX_CLIENTS;
			return response;
		}
		s3k_cidx_t pmp_cidx_storage = find_free_cidx();
		if (pmp_cidx_storage == S3K_CAP_CNT) {
			response.data[0] = FS_ERR_SERVER_MAX_CAPABILITIES;
			return response;
		}
		s3k_err_t err = s3k_cap_move(receive_cidx, pmp_cidx_storage);
		if (err) {
			response.data[0] = FS_ERR_SERVER_MAX_CAPABILITIES;
			return response;
		}
		clients[client_num].socket_tag = recv_msg.tag;
		clients[client_num].pmp_cap_idx = pmp_cidx_storage;
		clients[client_num].active = true;
		response.data[0] = FS_SUCCESS;
		alt_printf(PROCESS_NAME
			   ": CLIENT INITIALISED (%d), socket_tag=%d, pmp_cap_idx=%d, active=%d\n",
			   client_num, clients[client_num].socket_tag,
			   clients[client_num].pmp_cap_idx, clients[client_num].active);
	}
	return response;
}

size_t get_client(uint32_t tag)
{
	for (size_t i = 0; i < MAX_CLIENTS; i++) {
		if (clients[i].socket_tag == tag) {
			return i;
		}
	}
	return MAX_CLIENTS;
}

s3k_msg_t do_fs_client_finalize(s3k_reply_t recv_msg)
{
	s3k_msg_t response = {0};
	response.send_cap = false;
	size_t client_idx = get_client(recv_msg.tag);
	if (client_idx == MAX_CLIENTS) {
		response.data[0] = FS_ERR_NOT_CONNECTED;
		return response;
	}
	client_t *c = &clients[client_idx];
	s3k_err_t err = s3k_cap_delete(c->pmp_cap_idx);
	if (err == S3K_ERR_EMPTY || err == S3K_ERR_INVALID_INDEX) {
	} else if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	alt_printf(PROCESS_NAME
		   ": CLIENT FINALIZED (%d), socket_tag=%d, pmp_cap_idx=%d, active=%d\n",
		   client_idx, c->socket_tag, c->pmp_cap_idx, c->active);
	c->socket_tag = 0;
	c->active = false;
	c->pmp_cap_idx = 0;
	response.data[0] = FS_SUCCESS;
	return response;
}

s3k_msg_t do_fs_create_dir(s3k_reply_t recv_msg, s3k_cidx_t recv_cidx)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || cap.path.file || !cap.path.write) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	char buf[100];
	s3k_err_t err = s3k_path_read(recv_cidx, buf, sizeof(buf));
	if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	bool ensure_create = recv_msg.data[1];
	response.data[0] = create_dir(buf, ensure_create);
	alt_printf(PROCESS_NAME ": CLIENT (tag=%d) create_dir(path=%s, ensure_create=%d) = %d\n",
		   recv_msg.tag, buf, ensure_create, response.data[0]);
	return response;
}

s3k_msg_t do_fs_read_file(s3k_reply_t recv_msg, s3k_cidx_t recv_cidx, client_t *c)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || !cap.path.file || !cap.path.read) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	char buf[100];
	s3k_err_t err = s3k_path_read(recv_cidx, buf, sizeof(buf));
	if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	uint64_t file_offset = recv_msg.data[1];
	uint8_t *buf_ptr = (uint8_t *)recv_msg.data[2];
	uint64_t buf_len = recv_msg.data[3];
	if (!check_pmp(c->pmp_cap_idx, buf_ptr, buf_len, S3K_MEM_R)) {
		response.data[0] = FS_ERR_INVALID_MEMORY;
		return response;
	}
	uint32_t bytes_read = 0;
	fs_err_t fserr = read_file(buf, file_offset, buf_ptr, buf_len, &bytes_read);
	response.data[0] = fserr;
	if (!fserr) {
		response.data[1] = bytes_read;
	}
	alt_printf(
	    PROCESS_NAME
	    ": CLIENT (tag=%d) read_file(buf=%s, file_offset=%d, buf_ptr=0x%x, buf_len=%d) = %d\n",
	    recv_msg.tag, buf, file_offset, buf_ptr, buf_len, fserr);
	return response;
}

s3k_msg_t do_fs_write_file(s3k_reply_t recv_msg, s3k_cidx_t recv_cidx, client_t *c)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || !cap.path.file || !cap.path.write) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	char buf[100];
	s3k_err_t err = s3k_path_read(recv_cidx, buf, sizeof(buf));
	if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	uint64_t file_offset = recv_msg.data[1];
	uint8_t *buf_ptr = (uint8_t *)recv_msg.data[2];
	uint64_t buf_len = recv_msg.data[3];
	if (!check_pmp(c->pmp_cap_idx, buf_ptr, buf_len, S3K_MEM_W)) {
		response.data[0] = FS_ERR_INVALID_MEMORY;
		return response;
	}
	uint32_t bytes_written = 0;
	fs_err_t fserr = write_file(buf, file_offset, buf_ptr, buf_len, &bytes_written);
	response.data[0] = fserr;
	if (!fserr) {
		response.data[1] = bytes_written;
	}
	alt_printf(
	    PROCESS_NAME
	    ": CLIENT (tag=%d) write_file(buf=%s, file_offset=%d, buf_ptr=0x%x, buf_len=%d) = %d\n",
	    recv_msg.tag, buf, file_offset, buf_ptr, buf_len, fserr);
	return response;
}

s3k_msg_t do_fs_read_dir(s3k_reply_t recv_msg, s3k_cidx_t recv_cidx, client_t *c)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || cap.path.file || !cap.path.read) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	char buf[100];
	s3k_err_t err = s3k_path_read(recv_cidx, buf, sizeof(buf));
	if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	uint64_t child_idx = recv_msg.data[1];
	s3k_dir_entry_info_t *buf_ptr = (s3k_dir_entry_info_t *)recv_msg.data[2];
	uint64_t buf_len = sizeof(s3k_dir_entry_info_t);
	if (!check_pmp(c->pmp_cap_idx, (uint8_t *)buf_ptr, buf_len, S3K_MEM_R)) {
		response.data[0] = FS_ERR_INVALID_MEMORY;
		return response;
	}
	fs_err_t fserr = read_dir(buf, child_idx, buf_ptr);
	response.data[0] = fserr;
	alt_printf(PROCESS_NAME
		   ": CLIENT (tag=%d) read_dir(buf=%s, child_idx=%d, buf_ptr=0x%x) = %d\n",
		   recv_msg.tag, buf, child_idx, buf_ptr, fserr);
	return response;
}

s3k_msg_t do_fs_delete_entry(s3k_reply_t recv_msg, s3k_cidx_t recv_cidx)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || !cap.path.write) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	char buf[100];
	s3k_err_t err = s3k_path_read(recv_cidx, buf, sizeof(buf));
	if (err) {
		response.data[0] = FS_ERR_INVALID_CAPABILITY;
		return response;
	}
	response.data[0] = path_delete(buf);
	alt_printf(PROCESS_NAME ": CLIENT (tag=%d) path_delete(path=%s) = %d\n", recv_msg.tag, buf,
		   response.data[0]);
	return response;
}

bool ensure_client_connected(uint32_t client_tag)
{
	size_t client_idx = get_client(client_tag);
	if (client_idx == MAX_CLIENTS) {
		return false;
	}
	return true;
}

s3k_msg_t not_connected()
{
	s3k_msg_t response = {0};
	response.send_cap = false;
	response.data[0] = FS_ERR_NOT_CONNECTED;
	return response;
}

int main(void)
{
	s3k_sync_mem();

	alt_puts("Hello from file server");
	fs_init();
	alt_puts("File server initialized");
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
	s3k_cidx_t recv_cidx = find_free_cidx();
	if (recv_cidx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME
			   ": error: could not find a free cidx for file server to receive on\n");
		return -1;
	}

	s3k_reg_write(S3K_REG_SERVTIME, 100000);

	while (true) {
		s3k_reply_t recv_msg = s3k_sock_recv(server_cidx, recv_cidx);
	msg_received:
		if (recv_msg.err) {
			alt_printf(PROCESS_NAME ": error: s3k_sock_recv returned error %d\n",
				   recv_msg.err);
			continue;
		}
		alt_printf(PROCESS_NAME ": received from tag=%d, data=[%d, %d, %d, %d]",
			   recv_msg.tag, recv_msg.data[0], recv_msg.data[1], recv_msg.data[2],
			   recv_msg.data[3]);
		if (recv_msg.cap.type != S3K_CAPTY_NONE) {
			alt_putstr(", cap=(");
			print_cap(recv_msg.cap);
			alt_putchar(')');
		}
		alt_putchar('\n');

		bool should_receive_cap = fs_server_should_receive_cap(recv_msg.data[0]);
		bool received_cap = recv_msg.cap.type != S3K_CAPTY_NONE;
		if (should_receive_cap) {
			if (!received_cap) {
				alt_puts(PROCESS_NAME
					 ": did not receive cap when protocol indicates it");
			}
		} else {
			if (received_cap) {
				// Delete capabilities we should not have received
				s3k_cap_delete(recv_cidx);
			}
		}

		s3k_msg_t response = {0};
		switch ((fs_client_ops)recv_msg.data[0]) {
		case fs_client_init: {
			response = do_fs_client_init(recv_msg, recv_cidx);
		} break;
		case fs_client_finalize: {
			response = do_fs_client_finalize(recv_msg);
		} break;
		case fs_create_dir: {
			if (!ensure_client_connected(recv_msg.tag))
				response = not_connected();
			else
				response = do_fs_create_dir(recv_msg, recv_cidx);
		} break;
		case fs_read_file: {
			size_t client_idx = get_client(recv_msg.tag);
			if (client_idx == MAX_CLIENTS)
				response = not_connected();
			else
				response
				    = do_fs_read_file(recv_msg, recv_cidx, &clients[client_idx]);
		} break;
		case fs_write_file: {
			size_t client_idx = get_client(recv_msg.tag);
			if (client_idx == MAX_CLIENTS)
				response = not_connected();
			else
				response
				    = do_fs_write_file(recv_msg, recv_cidx, &clients[client_idx]);
		} break;
		case fs_delete_entry: {
			if (!ensure_client_connected(recv_msg.tag))
				response = not_connected();
			else
				response = do_fs_delete_entry(recv_msg, recv_cidx);
		} break;
		case fs_read_dir: {
			size_t client_idx = get_client(recv_msg.tag);
			if (client_idx == MAX_CLIENTS)
				response = not_connected();
			else
				response
				    = do_fs_read_dir(recv_msg, recv_cidx, &clients[client_idx]);
		} break;
		default: {
			response.send_cap = false;
			response.data[0] = FS_ERR_INVALID_OPERATION_CODE;
		} break;
		}
		alt_printf(PROCESS_NAME ": sending, data=[%d, %d, %d, %d]\n", response.data[0],
			   response.data[1], response.data[2], response.data[3]);
		recv_cidx = response.cap_idx;
		recv_msg = s3k_sock_sendrecv(server_cidx, &response);
		goto msg_received;
	}
}
