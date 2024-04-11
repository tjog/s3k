#include "../config.h"
#include "altc/altio.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"

#define PROCESS_NAME "app"

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
	s3k_msg_t msg;
	memcpy(msg.data, "pin", 4);
	msg.send_cap = false;
	msg.cap_idx = S3K_CAP_CNT - 1;
	msg.data[0] = fs_client_init;
	alt_printf(PROCESS_NAME ": s3k_sock_sendrecv starting\n");
	s3k_reply_t reply;
	do {
		reply = s3k_sock_sendrecv(fs_client_idx, &msg);
	} while (reply.err == S3K_ERR_NO_RECEIVER); // Wait for FS being initialised

	alt_printf(PROCESS_NAME ": s3k_sock_sendrecv finished\n");

	if (reply.err) {
		alt_printf(PROCESS_NAME ": error: s3k_sock_sendrecv returned error %d\n",
			   reply.err);
	} else if (reply.data[0] != FS_SUCCESS) {
		alt_printf(PROCESS_NAME ": error: file server returned error %d\n", reply.data[0]);
	} else {
		alt_printf(PROCESS_NAME ": received reply  from tag=%d, data=[%d, %d, %d, %d]",
			   reply.tag, reply.data[0], reply.data[1], reply.data[2], reply.data[3]);
		if (reply.cap.type != S3K_CAPTY_NONE) {
			alt_putstr(" cap = (");
			print_cap(reply.cap);
			alt_putchar(')');
		}
		alt_putchar('\n');
	}
}
