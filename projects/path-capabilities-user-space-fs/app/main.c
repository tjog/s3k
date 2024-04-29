#include "../config.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"

#define PROCESS_NAME "app"

uint8_t storage[4096];

s3k_cidx_t find_fs_client_cidx()
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

s3k_cidx_t find_mem_slice_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_MEMORY) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

s3k_cidx_t find_path_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_PATH) {
			return i;
		}
	}
	return S3K_CAP_CNT;
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

void print_reply(s3k_reply_t reply)
{
	if (reply.err) {
		alt_printf(PROCESS_NAME ": error: s3k_sock_sendrecv returned error %d\n",
			   reply.err);
	} else if (reply.data[0] != FS_SUCCESS) {
		alt_printf(PROCESS_NAME ": error: file server returned error %d\n", reply.data[0]);
	} else {
		alt_printf(PROCESS_NAME ": received reply from tag=%d, data=[%d, %d, %d, %d]",
			   reply.tag, reply.data[0], reply.data[1], reply.data[2], reply.data[3]);
		if (reply.cap.type != S3K_CAPTY_NONE) {
			alt_putstr(" cap = (");
			print_cap(reply.cap);
			alt_putchar(')');
		}
		alt_putchar('\n');
	}
}

int main(void)
{
	s3k_sync_mem();
	alt_puts("Hello from app");
	// dump_caps_range(0, S3K_CAP_CNT - 1);
	s3k_cidx_t fs_client_idx = find_fs_client_cidx();
	if (fs_client_idx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME ": error: could not find file server client socket\n");
		return -1;
	}

	s3k_cidx_t mem_slice = find_mem_slice_cidx();
	s3k_cidx_t free = find_free_cidx();
	// Create PMP capability for region of memory where FS communication stuff will be held
	s3k_cap_t cap
	    = s3k_mk_pmp(s3k_napot_encode((uint64_t)&storage, sizeof(storage)), S3K_MEM_RW);
	s3k_err_t err = s3k_cap_derive(mem_slice, free, cap);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}

	// dump_caps_range(0, S3K_CAP_CNT);

	// alt_printf(PROCESS_NAME ": sending cap_idx=%d\n", free);

	s3k_msg_t msg;
	msg.send_cap = true;
	msg.cap_idx = free;
	msg.data[0] = fs_client_init;
	alt_printf(PROCESS_NAME ": s3k_sock_sendrecv starting\n");
	s3k_reply_t reply;
	do {
		reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	} while (reply.err == S3K_ERR_NO_RECEIVER); // Wait for FS being initialised
	print_reply(reply);
	if (!reply.err) {
		alt_puts(PROCESS_NAME ": connected to file server");
	}

	s3k_cidx_t path_idx = find_path_cidx();
	if (path_idx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME ": error: could not find path capability\n");
		return -1;
	}
	s3k_cidx_t tmp_idx = free;

	// Create dir
	err = s3k_path_derive(path_idx, NULL, tmp_idx, PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_create_dir;
	msg.data[1] = false;
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		alt_puts(PROCESS_NAME ": file server created directory");
	}

	// Write file
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, FILE | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_write_file;
	msg.data[1] = 0;			    // File offset
	msg.data[2] = (uint64_t)(uint8_t *)storage; // Buf pointer
	ssize_t ret
	    = strscpy(storage, "Hello, this is something to test writing with.", sizeof(storage));
	if (ret < 0) {
		alt_printf(PROCESS_NAME ": error: strscpy returned negative number %d\n", ret);
		return -1;
	}
	msg.data[3] = ret; // Buf length / string to write to file
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		alt_printf(PROCESS_NAME ": file server wrote %d bytes\n", reply.data[1]);
	}

	// Read file
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, FILE | PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_read_file;
	msg.data[1] = 0;			    // File offset
	msg.data[2] = (uint64_t)(uint8_t *)storage; // Buf pointer
	msg.data[3] = sizeof(storage);		    // Buf length / maximmum read
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		uint64_t bytes_read = reply.data[1];
		if (storage[bytes_read - 1] != 0)
			storage[bytes_read] = 0;
		alt_printf(PROCESS_NAME ": file server read %d bytes:\n%s\n", bytes_read, storage);
	}

	// Read dir
	err = s3k_path_derive(path_idx, NULL, tmp_idx, PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_read_dir;
	msg.data[1] = 0;			    // File offset
	msg.data[2] = (uint64_t)(uint8_t *)storage; // Buf pointer
	msg.data[3] = 0;
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		s3k_dir_entry_info_t *dei = (s3k_dir_entry_info_t *)storage;
		alt_printf("Entry: fattrib=0x%X, fdate=0x%X, ftime=0x%X, fsize=%d fname=%s\n",
			   dei->fattrib, dei->fdate, dei->ftime, dei->fsize, dei->fname);
	}

	// Delete file
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, FILE | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_delete_entry;
	msg.data[1] = 0;
	msg.data[2] = 0;
	msg.data[3] = 0;
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		alt_puts(PROCESS_NAME ": file deleted");
	}

	// Delete directory
	err = s3k_path_derive(path_idx, NULL, tmp_idx, PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	msg.send_cap = true;
	msg.cap_idx = tmp_idx;
	msg.data[0] = fs_delete_entry;
	msg.data[1] = 0;
	msg.data[2] = 0;
	msg.data[3] = 0;
	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err || !reply.data[0]) {
		alt_puts(PROCESS_NAME ": directory deleted");
	}

	// Finalize
	msg.send_cap = false;
	msg.cap_idx = 0;
	msg.data[0] = fs_client_finalize;

	reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	print_reply(reply);
	if (!reply.err) {
		alt_puts(PROCESS_NAME ": connection closed");
	}
}
