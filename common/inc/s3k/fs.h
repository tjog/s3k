#pragma once

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