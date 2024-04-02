#pragma once
#include "s3k/types.h"

typedef enum {
	// Basic Info & Registers
	S3K_SYS_GET_INFO,  // Retrieve system information
	S3K_SYS_REG_READ,  // Read from a register
	S3K_SYS_REG_WRITE, // Write to a register
	S3K_SYS_SYNC,	   // Synchronize memory and time.

	// Capability Management
	S3K_SYS_CAP_READ,   // Read the properties of a capability.
	S3K_SYS_CAP_MOVE,   // Move a capability to a different slot.
	S3K_SYS_CAP_DELETE, // Delete a capability from the system.
	S3K_SYS_CAP_REVOKE, // Deletes derived capabilities.
	S3K_SYS_CAP_DERIVE, // Creates a new capability.

	// PMP calls
	S3K_SYS_PMP_LOAD,
	S3K_SYS_PMP_UNLOAD,

	// Monitor calls
	S3K_SYS_MON_SUSPEND,
	S3K_SYS_MON_RESUME,
	S3K_SYS_MON_STATE_GET,
	S3K_SYS_MON_YIELD,
	S3K_SYS_MON_REG_READ,
	S3K_SYS_MON_REG_WRITE,
	S3K_SYS_MON_CAP_READ,
	S3K_SYS_MON_CAP_MOVE,
	S3K_SYS_MON_PMP_LOAD,
	S3K_SYS_MON_PMP_UNLOAD,

	// Socket calls
	S3K_SYS_SOCK_SEND,
	S3K_SYS_SOCK_RECV,
	S3K_SYS_SOCK_SENDRECV,

	// Path+file calls
	S3K_SYS_PATH_READ,
	S3K_SYS_PATH_DERIVE,
	S3K_SYS_READ_FILE,
	S3K_SYS_WRITE_FILE,
	S3K_SYS_CREATE_DIR,
	S3K_SYS_PATH_DELETE,
	S3K_SYS_READ_DIR,
} s3k_syscall_t;

uint64_t s3k_get_pid(void);
uint64_t s3k_get_time(void);
uint64_t s3k_get_timeout(void);
uint64_t s3k_get_wcet(bool reset);
uint64_t s3k_reg_read(s3k_reg_t reg);
uint64_t s3k_reg_write(s3k_reg_t reg, uint64_t val);
void s3k_sync();
void s3k_sync_mem();
s3k_err_t s3k_cap_read(s3k_cidx_t idx, s3k_cap_t *cap);
s3k_err_t s3k_cap_move(s3k_cidx_t src, s3k_cidx_t dst);
s3k_err_t s3k_cap_delete(s3k_cidx_t idx);
s3k_err_t s3k_cap_revoke(s3k_cidx_t idx);
s3k_err_t s3k_cap_derive(s3k_cidx_t src, s3k_cidx_t dst, s3k_cap_t new_cap);
s3k_err_t s3k_pmp_load(s3k_cidx_t pmp_idx, s3k_pmp_slot_t pmp_slot);
s3k_err_t s3k_pmp_unload(s3k_cidx_t pmp_idx);
s3k_err_t s3k_mon_suspend(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_mon_resume(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_mon_state_get(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_state_t *state);
s3k_err_t s3k_mon_yield(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_mon_reg_read(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_reg_t reg, uint64_t *val);
s3k_err_t s3k_mon_reg_write(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_reg_t reg, uint64_t val);
s3k_err_t s3k_mon_cap_read(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t idx, s3k_cap_t *cap);
s3k_err_t s3k_mon_cap_move(s3k_cidx_t mon_idx, s3k_pid_t src_pid, s3k_cidx_t src_idx,
			   s3k_pid_t dst_pid, s3k_cidx_t dst_idx);
s3k_err_t s3k_mon_pmp_load(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t pmp_idx,
			   s3k_pmp_slot_t pmp_slot);
s3k_err_t s3k_mon_pmp_unload(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t pmp_idx);
s3k_err_t s3k_sock_send(s3k_cidx_t sock_idx, const s3k_msg_t *msg);
s3k_reply_t s3k_sock_recv(s3k_cidx_t sock_idx, s3k_cidx_t cap_cidx);
s3k_reply_t s3k_sock_sendrecv(s3k_cidx_t sock_idx, const s3k_msg_t *msg);

s3k_err_t s3k_try_cap_move(s3k_cidx_t src, s3k_cidx_t dst);
s3k_err_t s3k_try_cap_delete(s3k_cidx_t idx);
s3k_err_t s3k_try_cap_revoke(s3k_cidx_t idx);
s3k_err_t s3k_try_cap_derive(s3k_cidx_t src, s3k_cidx_t dst, s3k_cap_t new_cap);
s3k_err_t s3k_try_pmp_load(s3k_cidx_t pmp_idx, s3k_pmp_slot_t pmp_slot);
s3k_err_t s3k_try_pmp_unload(s3k_cidx_t pmp_idx);
s3k_err_t s3k_try_mon_suspend(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_try_mon_resume(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_try_mon_state_get(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_state_t *state);
s3k_err_t s3k_try_mon_yield(s3k_cidx_t mon_idx, s3k_pid_t pid);
s3k_err_t s3k_try_mon_reg_read(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_reg_t reg, uint64_t *val);
s3k_err_t s3k_try_mon_reg_write(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_reg_t reg, uint64_t val);
s3k_err_t s3k_try_mon_cap_read(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t idx, s3k_cap_t *cap);
s3k_err_t s3k_try_mon_cap_move(s3k_cidx_t mon_idx, s3k_pid_t src_pid, s3k_cidx_t src_idx,
			       s3k_pid_t dst_pid, s3k_cidx_t dst_idx);
s3k_err_t s3k_try_mon_pmp_load(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t pmp_idx,
			       s3k_pmp_slot_t pmp_slot);
s3k_err_t s3k_try_mon_pmp_unload(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t pmp_idx);
s3k_err_t s3k_try_sock_send(s3k_cidx_t sock_idx, const s3k_msg_t *msg);
s3k_reply_t s3k_try_sock_recv(s3k_cidx_t sock_idx, s3k_cidx_t cap_cidx);
s3k_reply_t s3k_try_sock_sendrecv(s3k_cidx_t sock_idx, const s3k_msg_t *msg);

/**
 * Used to read out the absolute path of a path capability into a buffer,
 * Useful for debugging, does not touch file system state, only reads kernel
 * saved buffer.
*/
s3k_err_t s3k_path_read(s3k_cidx_t idx, char *buf, size_t n);
/**
 * Derive new path capabilities using this function rather than the generic 
 * capability derive, necessary to take a path and convert to a tag, which the
 * kernel must be responsible for rather than user-space precreating the full
 * capability.
*/
s3k_err_t s3k_path_derive(s3k_cidx_t src, const char *path, s3k_cidx_t dest,
			  s3k_path_flags_t flags);
/**
 * Read a file at the specified offset, EOF is detected by checking bytes_read < buf_size.
 * No persistent file descriptors exist.
*/
s3k_err_t s3k_read_file(s3k_cidx_t file, uint32_t offset, uint8_t *buf, uint32_t buf_size,
			uint32_t *bytes_read);
/**
 * Write to a file at the specified offset, partial writes are detected by checking bytes_read < buf_size.
 * No persistent file descriptors exist.
*/
s3k_err_t s3k_write_file(s3k_cidx_t file, uint32_t offset, uint8_t *buf, uint32_t buf_size,
			 uint32_t *bytes_written);
/**
 * Creates a path physically on disk, errors if a parent path does not exist.
 * Ensure create will error instead of silently succeed when the directory
 * already exists. It will not silently succeed if the existing directory entry
 * is a file rather than a directory.
*/
s3k_err_t s3k_create_dir(s3k_cidx_t idx, bool ensure_create);
/**
 * Deletes a path if on disk, returns an error if not found or delete operation is unsuccesful.
 * If directory, it must be empty before deletion can succeed.
 * Note this does not delete or revoke the referenced capability.
*/
s3k_err_t s3k_path_delete(s3k_cidx_t idx);
/**
 * Retrieve the directory entry information at some index and store in the
 * provided info structure.
*/
s3k_err_t s3k_read_dir(s3k_cidx_t directory, size_t dir_entry_idx, s3k_dir_entry_info_t *out);
