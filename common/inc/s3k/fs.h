#pragma once

#include <stdbool.h>

typedef enum {
	FS_SUCCESS,
	FS_ERR_FILE_OPEN,
	FS_ERR_FILE_SEEK,
	FS_ERR_FILE_READ,
	FS_ERR_FILE_WRITE,
	FS_ERR_PATH_EXISTS,
	FS_ERR_PATH_STAT,
	FS_ERR_INVALID_INDEX,
	FS_ERR_INVALID_OPERATION_CODE,
	FS_ERR_INVALID_CAPABILITY,
	FS_ERR_MAX_CLIENTS,
	FS_ERR_SERVER_MAX_CAPABILITIES,
	FS_ERR_NOT_CONNECTED,
	FS_ERR_LOAD_PMP,
	FS_ERR_INVALID_MEMORY,
} fs_err_t;

/* data[0] is the operation code */
typedef enum {
	fs_client_init, /* Connect, give PMP capability to region containing read/write buffer */
	fs_client_finalize, /* Disconnect, delete PMP capability */
	fs_read_file, /* Capability is of type path, data[1] is the file offset, data[2] is a ptr to a buf, data[3] is a length. */
	fs_write_file, /* Same protocol as fs_read_file */
	fs_create_dir, /* Sent capability is of type path, data[1] is true/false if ensure_create */
	fs_delete_entry, /* Sent capability is of type path */
	fs_read_dir, /* Sent capability is of type path, data[1] is the child index to read, data[2] is a ptr to a s3k_dir_entry_info_t */
} fs_client_ops;

inline bool fs_server_should_receive_cap(fs_client_ops op)
{
	switch (op) {
	case fs_client_finalize:
		return false;
	default:
		return true;
	}
}