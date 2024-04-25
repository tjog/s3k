#include "../config.h"
#include "../sign_protocol.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/s3k.h"
#include "sha256.h"

#define PROCESS_NAME "app"

s3k_cidx_t find_sign_client_cidx()
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

s3k_err_t read_pub_key(s3k_cidx_t pub_cidx, rsa_public_key_t *pub_key)
{
	char buf[100];
	volatile uint32_t bytes_read = 0;
	s3k_err_t err = s3k_read_file(pub_cidx, 0, buf, sizeof(buf), &bytes_read);
	if (err)
		return err;
	if (bytes_read == 0) {
		alt_puts(PROCESS_NAME ": file empty (bytes_read=0)");
		return S3K_ERR_FILE_READ;
	}
	buf[bytes_read] = 0;
	char *p = buf;
	alt_printf("(p=%s)\n", buf);
	long n = atol(p);
	if (n < 0) {
		alt_printf(PROCESS_NAME ": invalid n from pub key (n=%d)\n", n);
		return S3K_ERR_INVALID_STATE;
	}
	alt_printf("(n=%d)\n", n);
	size_t max_iter = 100;
	while (max_iter && *p++ != ' ')
		max_iter--;
	if (!max_iter) {
		return S3K_ERR_FILE_READ;
	}
	int e = atoi(p);
	if (e < 0) {
		alt_printf(PROCESS_NAME ": invalid e from pub key (e=%d)\n", e);
		return S3K_ERR_INVALID_STATE;
	}
	alt_printf("(e=%d)\n", e);
	pub_key->n = (uint64_t)n;
	pub_key->e = (uint64_t)e;
	return S3K_SUCCESS;
}

static char read_buf[513];

s3k_err_t read_and_digest_file(s3k_cidx_t path, SHA256_CTX *ctx)
{
	read_buf[512] = 0;
	uint32_t offset = 0;
	while (true) {
		volatile uint32_t bytes_read = 0;
		s3k_err_t err
		    = s3k_read_file(path, offset, read_buf, sizeof(read_buf) - 1, &bytes_read);
		if (err) {
			alt_printf("s3k_read_file error: %d\n", err);
			return err;
		}
		if (bytes_read > 0) {
			alt_printf("Read 0x%x bytes\n", bytes_read);
			offset += bytes_read;
			sha256_update(ctx, read_buf, bytes_read);
			if (bytes_read < sizeof(read_buf) - 1) {
				// End of file
				break;
			}
		}
	}
}

static SHA256_CTX ctx;
static BYTE digest_buf[SHA256_BLOCK_SIZE];

void print_reply(s3k_reply_t reply)
{
	if (reply.err) {
		alt_printf(PROCESS_NAME ": error: s3k_sock_sendrecv returned error %d\n",
			   reply.err);
	} else if (reply.data[0] != 0) {
		alt_printf(PROCESS_NAME ": error: sign server returned error %d\n", reply.data[0]);
	} else {
		alt_printf(PROCESS_NAME ": received reply from tag=%d, data=[%d, 0x%x, 0x%x, 0x%x]",
			   reply.tag, reply.data[0], reply.data[1], reply.data[2], reply.data[3]);
		if (reply.cap.type != S3K_CAPTY_NONE) {
			alt_putstr(" cap = (");
			print_cap(reply.cap);
			alt_putchar(')');
		}
		alt_putchar('\n');
	}
}

#define ERR_IF_EQL(X, Y)                                                                        \
	do {                                                                                    \
		if (X == Y) {                                                                   \
			alt_printf(PROCESS_NAME ": error: unexpected equals %d == %d\n", X, Y); \
			return -1;                                                              \
		}                                                                               \
	} while (false);

int main(void)
{
	alt_puts("Hello from app");
	s3k_sync_mem();

	dump_caps_range(PROCESS_NAME, 0, S3K_CAP_CNT - 1);
	s3k_cidx_t sign_client_idx = find_sign_client_cidx();
	ERR_IF_EQL(sign_client_idx, S3K_CAP_CNT);

	s3k_cidx_t home_dir_cidx = find_path_cidx();
	ERR_IF_EQL(home_dir_cidx, S3K_CAP_CNT);

	s3k_cidx_t free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);

	s3k_err_t err;
#if 1
	// Test escaping the home directory
	do {
		err = s3k_path_derive(home_dir_cidx, "../sign", free_cidx, PATH_READ | PATH_WRITE);
	} while (err && err == S3K_ERR_PREEMPTED);
	if (!err) {
		alt_puts(
		    PROCESS_NAME
		    ": error: s3k_path_derive to escape home directory unexpectdely succedeed");
		return -1;
	} else {
		alt_printf(
		    PROCESS_NAME
		    ": error: s3k_path_derive to escape home directory failed as expected with error: %d\n",
		    err);
	}
#endif

	// Create PATH capability to document to be signed
	do {
		err = s3k_path_derive(home_dir_cidx, "Test.doc", free_cidx,
				      FILE | PATH_READ | PATH_WRITE);
	} while (err && err == S3K_ERR_PREEMPTED);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_path_derive returned error %d\n", err);
		return -1;
	}
	s3k_cidx_t testdoc_cidx = free_cidx;
	free_cidx = find_free_cidx();
	ERR_IF_EQL(free_cidx, S3K_CAP_CNT);

	volatile uint32_t bytes_written = 0;
	char b[] = "This is a test document to be signed by the signing server. Blablabla";
	err = s3k_write_file(testdoc_cidx, 0, b, sizeof(b), &bytes_written);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_write_file returned error %d\n", err);
		return -1;
	}

	err = s3k_path_derive(testdoc_cidx, NULL, free_cidx, FILE | PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_path_derive testdoc_cidx returned error %d\n",
			   err);
		return -1;
	}

	// // alt_printf(PROCESS_NAME ": sending cap_idx=%d\n", free);

	s3k_msg_t msg;
	msg.send_cap = true;
	msg.cap_idx = free_cidx;
	msg.data[0] = sign_client_sign_file;
	alt_printf(PROCESS_NAME ": s3k_sock_sendrecv sign_client_sign_file starting\n");
	s3k_reply_t reply;
	do {
		reply = s3k_sock_sendrecv(sign_client_idx, &msg);
	} while (reply.err == S3K_ERR_NO_RECEIVER); // Wait for SIGN being initialised
	print_reply(reply);
	if (reply.err || reply.data[0] != SIGN_SUCCESS)
		return -1;
#define RECEIVED_DIGEST_LENGTH 24
	uint8_t signed_digest[RECEIVED_DIGEST_LENGTH];
	*(uint64_t *)&signed_digest[0] = reply.data[1];
	*(uint64_t *)&signed_digest[1 * sizeof(uint64_t)] = reply.data[2];
	*(uint64_t *)&signed_digest[2 * sizeof(uint64_t)] = reply.data[3];
	// Dump sha256 digest for debug
	char hex[] = "0123456789abcdef";
	alt_putstr(PROCESS_NAME ": received SHA256 encrypted=");
	for (uint8_t *i = signed_digest; i < &signed_digest[RECEIVED_DIGEST_LENGTH]; i++) {
		uint8_t a = *i;
		alt_putchar(hex[a >> 4]);
		alt_putchar(hex[a & 0xF]);
	}
	alt_putchar('\n');

	msg.send_cap = false;
	msg.data[0] = sign_client_request_public_key;
	alt_printf(PROCESS_NAME ": s3k_sock_sendrecv sign_client_request_public_key starting\n");
	do {
		reply = s3k_sock_sendrecv(sign_client_idx, &msg);
	} while (reply.err == S3K_ERR_NO_RECEIVER);
	print_reply(reply);
	s3k_cidx_t pub_cidx = msg.cap_idx;
	char buf[100];
	err = s3k_path_read(pub_cidx, buf, sizeof(buf));
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_path_read returned error %d\n", err);
		return -1;
	}
	alt_printf(PROCESS_NAME ": received capability for path '%s' from SIGN server\n", buf);

	rsa_public_key_t rsa_pub;
	err = read_pub_key(pub_cidx, &rsa_pub);
	if (err) {
		alt_printf(PROCESS_NAME ": error: read_pub_key returned error %d\n", err);
		return -1;
	}

	// Do SHA256 digest ourselves and check it matches with the (decrypted) signed digest.
	sha256_init(&ctx);
	err = read_and_digest_file(testdoc_cidx, &ctx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: read_and_digest_file returned error %d\n", err);
		return -1;
	}
	alt_printf("finished digesting file suceess\n");

	sha256_final(&ctx, digest_buf);

	// Dump sha256 for debug
	alt_putstr(PROCESS_NAME ": OWN SHA256=");
	for (uint8_t *i = digest_buf; i < &digest_buf[SHA256_BLOCK_SIZE]; i++) {
		uint8_t a = *i;
		alt_putchar(hex[a >> 4]);
		alt_putchar(hex[a & 0xF]);
	}
	alt_putchar('\n');
	// Use pub key to transform signed_digest into what digest should be.
	rsa_mod_pow(rsa_pub.e, rsa_pub.n, signed_digest, sizeof(signed_digest));

	// Dump "decrypted" sha256 for debug
	alt_putstr(PROCESS_NAME ": received SHA256 decrypted=");
	for (uint8_t *i = signed_digest; i < &signed_digest[RECEIVED_DIGEST_LENGTH]; i++) {
		uint8_t a = *i;
		alt_putchar(hex[a >> 4]);
		alt_putchar(hex[a & 0xF]);
	}
	alt_putchar('\n');

	if (memcmp(signed_digest, digest_buf, sizeof(signed_digest)) != 0) {
		alt_puts(
		    PROCESS_NAME
		    ": the received signed payload was not validated with the SIGN public key, they differ.");
	} else {
		alt_puts(PROCESS_NAME ": the document signature is valid");
	}
}
