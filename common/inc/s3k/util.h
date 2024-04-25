#pragma once
#include "altc/altio.h"
#include "s3k/types.h"

s3k_cap_t s3k_mk_time(s3k_hart_t hart, s3k_time_slot_t bgn,
		      s3k_time_slot_t end);
s3k_cap_t s3k_mk_memory(s3k_addr_t bgn, s3k_addr_t end, s3k_rwx_t rwx);
s3k_cap_t s3k_mk_pmp(s3k_napot_t napot_addr, s3k_rwx_t rwx);
s3k_cap_t s3k_mk_monitor(s3k_pid_t bgn, s3k_pid_t end);
s3k_cap_t s3k_mk_channel(s3k_chan_t bgn, s3k_chan_t end);
s3k_cap_t s3k_mk_socket(s3k_chan_t chan, s3k_ipc_mode_t mode,
			s3k_ipc_perm_t perm, uint32_t tag);

bool s3k_is_valid(s3k_cap_t a);
bool s3k_is_parent(s3k_cap_t a, s3k_cap_t b);
bool s3k_is_derivable(s3k_cap_t a, s3k_cap_t b);

void s3k_napot_decode(s3k_napot_t napot_addr, s3k_addr_t *base, s3k_addr_t *size);
s3k_napot_t s3k_napot_encode(s3k_addr_t base, s3k_addr_t size);

static inline bool s3k_is_ready(s3k_state_t state)
{
	return state == 0;
}

static inline bool s3k_is_busy(s3k_state_t state)
{
	return state & S3K_PSF_BUSY;
}

static inline bool s3k_is_blocked(s3k_state_t state, s3k_chan_t *chan)
{
	*chan = state >> 32;
	return state & S3K_PSF_BLOCKED;
}

static inline bool s3k_is_suspended(s3k_state_t state)
{
	return state == S3K_PSF_SUSPENDED;
}

static void print_cap(s3k_cap_t cap)
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

static void dump_caps_range(char *prefix, s3k_cidx_t start, s3k_cidx_t end)
{
	for (size_t i = start; i <= end; i++) {
		alt_printf("%s: %d: ", prefix, i);
		s3k_cap_t cap;
		s3k_err_t err = s3k_cap_read(i, &cap);
		if (!err) {
			print_cap(cap);
			if (cap.type == S3K_CAPTY_PATH) {
				char buf[S3K_MAX_PATH_LEN];
				s3k_err_t err = s3k_path_read(i, buf, sizeof(buf));
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

static void mon_dump_caps_range(s3k_pid_t self_pid, s3k_cidx_t mon_idx, s3k_pid_t pid,
				s3k_cidx_t start, s3k_cidx_t end)
{
	alt_printf("Dumping caps for PID %d for idx [%d,%d]:\n", pid, start, end);
	for (size_t i = start; i <= end; i++) {
		alt_printf("%d: ", i);
		s3k_cap_t cap;
		s3k_err_t err = (pid == self_pid) ? s3k_cap_read(i, &cap) :
						    s3k_mon_cap_read(mon_idx, pid, i, &cap);
		if (!err) {
			print_cap(cap);
			if (cap.type == S3K_CAPTY_PATH) {
				char buf[S3K_MAX_PATH_LEN];
				s3k_err_t err
				    = (pid == self_pid) ?
					  s3k_path_read(i, buf, sizeof(buf)) :
					  s3k_mon_path_read(mon_idx, pid, i, buf, sizeof(buf));
				if (!err) {
					alt_putstr(" (='");
					alt_putstr(buf);
					alt_putstr("')");
				} else {
					alt_printf(" (Path read returned error %d)", err);
				}
			}
		} else {
			alt_putstr("NONE");
			alt_printf(" (Error from s3k_mon_cap_read: 0x%X)", err);
		}
		alt_putchar('\n');
	}
}
