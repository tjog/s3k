#include "../config.h"
#include "../sign_protocol.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"
#include "sha256.h"

#if !SIGN_DEBUG
#define alt_printf(...)
#define alt_puts(...)
#define alt_putstr(...)
#define alt_putchar(...)
#define alt_puts(...)
#endif

#define PROCESS_NAME "sign"

uint8_t storage[4096];	 /* FS CLIENT STORAGE */
s3k_cidx_t fs_sock_cidx; /* FS SOCKET CIDX*/

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

s3k_cidx_t find_sign_socket_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_SOCKET && c.sock.chan == SIGN_CHANNEL) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

s3k_cidx_t find_fs_socket_cidx()
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

bool string_ends_with(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return false;
	size_t lenstr = alt_strlen(str);
	size_t lensuffix = alt_strlen(suffix);
	if (lensuffix > lenstr)
		return false;
	return alt_strcmp(str + lenstr - lensuffix, suffix) == 0;
}

s3k_err_t find_suffix_in_dir_and_derive(s3k_cidx_t dir, char *suffix, s3k_cidx_t dest,
					s3k_path_flags_t flags)
{
	s3k_dir_entry_info_t *dei = (s3k_dir_entry_info_t *)&storage;
	s3k_err_t err;
	s3k_cidx_t send_cidx = find_free_cidx();
	for (size_t i = 0;; i++) {
		s3k_err_t err = s3k_path_derive(dir, NULL, send_cidx, PATH_READ);
		if (err)
			return err;
		s3k_reply_t reply;
		do {
			reply = send_fs_read_dir(fs_sock_cidx, send_cidx, i, dei);
		} while (reply.err && (reply.err == S3K_ERR_TIMEOUT));
		if (reply.err || reply.data[0]) {
			alt_printf(PROCESS_NAME
				   ": send_fs_read_dir error, reply.err=%d, reply.data[0]=%d\n",
				   reply.err, reply.data[0]);
			return reply.err ? reply.err : S3K_ERR_INVALID_PATH;
		}
		alt_printf("Checking: '%s' ... %s\n", dei->fname,
			   string_ends_with(dei->fname, suffix) ? "true" : "false");
		if (string_ends_with(dei->fname, suffix)) {
			if (dei->fattrib & AM_DIR) {
				flags &= ~(FILE);
			} else {
				flags |= FILE;
			}
			do {
				err = s3k_path_derive(dir, dei->fname, dest, flags);
			} while (err && err == S3K_ERR_PREEMPTED);
			if (err)
				return err;
			return S3K_SUCCESS;
		}
	}
}

static rsa_private_key_t priv_key;

s3k_err_t read_priv_key(s3k_cidx_t priv_cidx, rsa_private_key_t *priv_key)
{
	s3k_cidx_t send_cidx = find_free_cidx();
	if (send_cidx == S3K_CAP_CNT) {
		return S3K_ERR_INVALID_INDEX;
	}

	s3k_err_t err = s3k_path_derive(priv_cidx, NULL, send_cidx, FILE | PATH_READ);
	if (err)
		return err;
	s3k_reply_t reply = send_fs_read_file(fs_sock_cidx, send_cidx, 0, storage, sizeof(storage));
	if (reply.err || reply.data[0]) {
		alt_printf(PROCESS_NAME
			   ": send_fs_read_file error, reply.err=%d, reply.data[0]=%d\n",
			   reply.err, reply.data[0]);
		return reply.err ? reply.err : S3K_ERR_INVALID_PATH;
	}
	print_reply(reply);
	uint32_t bytes_read = reply.data[1];
	char *buf = storage;

	buf[bytes_read] = 0;
	char *p = buf;
	alt_printf("(p=%s)\n", buf);
	long n = atol(p);
	if (n < 0) {
		alt_printf(PROCESS_NAME ": invalid n from priv key (n=%d)\n", n);
		return S3K_ERR_INVALID_STATE;
	}
	alt_printf("(n=%d)\n", n);
	size_t max_iter = 100;
	while (max_iter && *p++ != ' ')
		max_iter--;
	if (!max_iter) {
		return S3K_ERR_INVALID_INDEX;
	}
	int d = atoi(p);
	if (d < 0) {
		alt_printf(PROCESS_NAME ": invalid d from priv key (d=%d)\n", d);
		return S3K_ERR_INVALID_STATE;
	}
	alt_printf("(d=%d)\n", d);
	priv_key->n = (uint64_t)n;
	priv_key->d = (uint64_t)d;
	return S3K_SUCCESS;
}

#define ERR_IF_EQL(X, Y)                                                                        \
	do {                                                                                    \
		if (X == Y) {                                                                   \
			alt_printf(PROCESS_NAME ": error: unexpected equals %d == %d\n", X, Y); \
			return -1;                                                              \
		}                                                                               \
	} while (false);

static s3k_msg_t do_sign_file(s3k_reply_t recv_msg, s3k_cidx_t received_cap_idx);
static s3k_msg_t do_request_public_key(s3k_reply_t recv_msg, s3k_cidx_t pub_cidx);

int main(void)
{
	alt_puts("Hello from sign");

	s3k_cidx_t home_dir_cidx = find_path_cidx();
	ERR_IF_EQL(home_dir_cidx, S3K_CAP_CNT);
	s3k_cidx_t free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);
	fs_sock_cidx = find_fs_socket_cidx();
	ERR_IF_EQL(fs_sock_cidx, S3K_CAP_CNT);
	s3k_cidx_t mem_slice = find_mem_slice_cidx();
	ERR_IF_EQL(mem_slice, S3K_CAP_CNT);

	// Create PMP capability for region of memory where FS communication stuff will be held
	s3k_cap_t cap
	    = s3k_mk_pmp(s3k_napot_encode((uint64_t)&storage, sizeof(storage)), S3K_MEM_RW);
	s3k_err_t err = s3k_cap_derive(mem_slice, free_cidx, cap);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	s3k_cidx_t fs_pmp_cidx = free_cidx;
	free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);

	fs_sock_cidx = find_fs_socket_cidx();
	ERR_IF_EQL(fs_sock_cidx, S3K_CAP_CNT);
	s3k_reply_t reply;
	do {
		reply = send_fs_client_init(fs_sock_cidx, fs_pmp_cidx);
	} while (reply.err && reply.err == S3K_ERR_NO_RECEIVER);
	print_reply(reply);
	if (!reply.err) {
		alt_puts(PROCESS_NAME ": connected to file server");
	}

	// Priv cidx
	err = find_suffix_in_dir_and_derive(home_dir_cidx, ".PRI", free_cidx, FILE | PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": could not find '.PRI' suffix, error: %d\n", err);
		return -1;
	}
	s3k_cidx_t priv_cidx = free_cidx;
	free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);

	// Pub cidx
	err = find_suffix_in_dir_and_derive(home_dir_cidx, ".PUB", free_cidx, FILE | PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": could not find '.PUB' suffix, error: %d\n", err);
		return -1;
	}
	s3k_cidx_t pub_cidx = free_cidx;
	free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);

	// Read priv key into memory
	err = read_priv_key(priv_cidx, &priv_key);
	if (err) {
		alt_printf(PROCESS_NAME ": read_priv_key error: %d\n", err);
		return -1;
	}

	// Find what cidx our server socket is
	s3k_cidx_t server_cidx = find_sign_socket_cidx();
	if (server_cidx == S3K_CAP_CNT) {
		alt_puts(PROCESS_NAME ": error: could not find file server's socket");
		return -1;
	}
	// Find a free cidx to receive clients capabilities
	s3k_cidx_t recv_cidx = find_free_cidx();
	if (recv_cidx == S3K_CAP_CNT) {
		alt_puts(PROCESS_NAME
			 ": error: could not find a free cidx for file server to receive on");
		return -1;
	}

	s3k_reg_write(S3K_REG_SERVTIME, 15000);

	alt_puts(PROCESS_NAME ": starting server loop");
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

		s3k_msg_t response = {0};
		switch ((sign_client_ops)recv_msg.data[0]) {
		case sign_client_sign_file: {
			response = do_sign_file(recv_msg, recv_cidx);
		} break;
		case sign_client_request_public_key: {
			response = do_request_public_key(recv_msg, pub_cidx);
		} break;
		default: {
			response.send_cap = false;
			response.data[0] = SIGN_ERR_INVALID_OPERATION_CODE;
		} break;
		}
		alt_printf(PROCESS_NAME ": sending, data=[%d, 0x%x, 0x%x, 0x%x]\n",
			   response.data[0], response.data[1], response.data[2], response.data[3]);
		recv_msg = s3k_sock_sendrecv(server_cidx, &response);
		goto msg_received;
	}
}

s3k_err_t read_and_digest_file(s3k_cidx_t path, SHA256_CTX *ctx)
{
	// read_buf[512] = 0;
	uint32_t offset = 0;
	s3k_cidx_t send_cidx = find_free_cidx();
	while (true) {
		s3k_err_t err = s3k_path_derive(path, NULL, send_cidx, FILE | PATH_READ);
		if (err)
			return err;
		s3k_reply_t reply
		    = send_fs_read_file(fs_sock_cidx, send_cidx, offset, storage, sizeof(storage));
		if (reply.err || reply.data[0]) {
			// Error
			alt_printf(PROCESS_NAME
				   ": send_fs_read_file error, reply.err=%d, reply.data[0]=%d\n",
				   reply.err, reply.data[0]);
			return reply.err ? reply.err : S3K_ERR_INVALID_PATH;
		}
		uint32_t bytes_read = reply.data[1];

		if (bytes_read > 0) {
			alt_printf("Read 0x%x bytes\n", bytes_read);
			offset += bytes_read;
			sha256_update(ctx, storage, bytes_read);
			if (bytes_read < sizeof(storage) - 1) {
				// End of file
				break;
			}
		}
	}
	return S3K_SUCCESS;
}

static SHA256_CTX ctx;
static BYTE sign_buf[SHA256_BLOCK_SIZE];

s3k_msg_t do_sign_file(s3k_reply_t recv_msg, s3k_cidx_t received_cap_idx)
{
	s3k_msg_t response = {0};
	response.send_cap = false;

	s3k_cap_t cap = recv_msg.cap;
	if (cap.type != S3K_CAPTY_PATH || !cap.path.file || !cap.path.read) {
		response.data[0] = SIGN_ERR_INVALID_CAPABILITY;
		return response;
	}
	sha256_init(&ctx);
	s3k_err_t err = read_and_digest_file(received_cap_idx, &ctx);
	if (err) {
		response.data[0] = SIGN_ERR_FILE_READ;
		return response;
	}
	alt_printf("finished digesting file suceess\n");

	sha256_final(&ctx, sign_buf);

	char hex[] = "0123456789abcdef";
	// Dump sha256 for debug
	alt_putstr(PROCESS_NAME ": SHA256=");
	for (uint8_t *i = sign_buf; i < &sign_buf[SHA256_BLOCK_SIZE]; i++) {
		uint8_t a = *i;
		alt_putchar(hex[a >> 4]);
		alt_putchar(hex[a & 0xF]);
	}
	alt_putchar('\n');

	rsa_mod_pow(priv_key.d, priv_key.n, sign_buf, SHA256_BLOCK_SIZE);

	// Dump sha256 digest after encryption for debug
	alt_putstr(PROCESS_NAME ": SHA256 encrypted=");
	for (uint8_t *i = sign_buf; i < &sign_buf[SHA256_BLOCK_SIZE]; i++) {
		uint8_t a = *i;
		alt_putchar(hex[a >> 4]);
		alt_putchar(hex[a & 0xF]);
	}
	alt_putchar('\n');

	response.data[0] = SIGN_SUCCESS;
	// Truncate from 32 byte to 24 byte
	response.data[1] = *(uint64_t *)&sign_buf[0];
	response.data[2] = *(uint64_t *)&sign_buf[1 * sizeof(uint64_t)];
	response.data[3] = *(uint64_t *)&sign_buf[2 * sizeof(uint64_t)];

	return response;
}

s3k_msg_t do_request_public_key(s3k_reply_t recv_msg, s3k_cidx_t pub_cidx)
{
	s3k_msg_t response = {0};
	response.send_cap = false;
	s3k_cidx_t free = find_free_cidx();
	if (free == S3K_CAP_CNT) {
		response.data[0] = SIGN_ERR_SERVER_OUT_OF_CAPABILITY_SLOTS;
		return response;
	}
	s3k_err_t err = s3k_path_derive(pub_cidx, NULL, free, FILE | PATH_READ);
	if (err) {
		response.data[0] = SIGN_ERR_SERVER_DERIVATION;
		return response;
	}

	response.send_cap = true;
	response.cap_idx = free;
	return response;
}
