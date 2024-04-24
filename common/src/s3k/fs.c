#include "s3k/fs.h"

s3k_reply_t send_fs_client_init(s3k_cidx_t sock_idx, s3k_cidx_t pmp_cap_idx)
{
	const s3k_msg_t msg = {
	    .cap_idx = pmp_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_client_init,
		     [1] = 0,
		     [2] = 0,
		     [3] = 0,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_client_finalize(s3k_cidx_t sock_idx)
{
	const s3k_msg_t msg = {
	    .cap_idx = 0,
	    .send_cap = false,
	    .data = {
		     [0] = fs_client_finalize,
		     [1] = 0,
		     [2] = 0,
		     [3] = 0,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_read_file(s3k_cidx_t sock_idx, s3k_cidx_t path_cap_idx, uint32_t file_offset,
			      uint8_t *buf, size_t buf_len)
{
	const s3k_msg_t msg = {
	    .cap_idx = path_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_read_file,
		     [1] = file_offset,
		     [2] = (uint64_t)buf,
		     [3] = buf_len,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_write_file(s3k_cidx_t sock_idx, s3k_cidx_t path_cap_idx, uint32_t file_offset,
			       uint8_t *buf, size_t buf_len)
{
	const s3k_msg_t msg = {
	    .cap_idx = path_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_write_file,
		     [1] = file_offset,
		     [2] = (uint64_t)buf,
		     [3] = buf_len,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_create_dir(s3k_cidx_t sock_idx, s3k_cidx_t path_cap_idx, bool ensure_create)
{
	const s3k_msg_t msg = {
	    .cap_idx = path_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_create_dir,
		     [1] = ensure_create,
		     [2] = 0,
		     [3] = 0,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_delete_entry(s3k_cidx_t sock_idx, s3k_cidx_t path_cap_idx)
{
	const s3k_msg_t msg = {
	    .cap_idx = path_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_delete_entry,
		     [1] = 0,
		     [2] = 0,
		     [3] = 0,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}

s3k_reply_t send_fs_read_dir(s3k_cidx_t sock_idx, s3k_cidx_t path_cap_idx, size_t child_idx,
			     s3k_dir_entry_info_t *dest)
{
	const s3k_msg_t msg = {
	    .cap_idx = path_cap_idx,
	    .send_cap = true,
	    .data = {
		     [0] = fs_read_dir,
		     [1] = child_idx,
		     [2] = (uint64_t)dest,
		     [3] = 0,
		     }
	     };
	return s3k_sock_sendrecv(sock_idx, &msg);
}
