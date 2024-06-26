#include "cap_util.h"

#include "pmp.h"

cap_t cap_mk_time(hart_t hart, time_slot_t bgn, time_slot_t end)
{
	cap_t cap;
	cap.type = CAPTY_TIME;
	cap.time.hart = hart;
	cap.time.bgn = bgn;
	cap.time.mrk = bgn;
	cap.time.end = end;
	return cap;
}

cap_t cap_mk_memory(addr_t bgn, addr_t end, rwx_t rwx)
{
	uint64_t tag = bgn >> MAX_BLOCK_SIZE;
	cap_t cap;
	cap.mem.type = CAPTY_MEMORY;
	cap.mem.tag = tag;
	cap.mem.bgn = (bgn - (tag << MAX_BLOCK_SIZE)) >> MIN_BLOCK_SIZE;
	cap.mem.end = (end - (tag << MAX_BLOCK_SIZE)) >> MIN_BLOCK_SIZE;
	cap.mem.mrk = cap.mem.bgn;
	cap.mem.rwx = rwx;
	cap.mem.lck = false;
	return cap;
}

cap_t cap_mk_pmp(napot_t addr, rwx_t rwx)
{
	cap_t cap;
	cap.pmp.type = CAPTY_PMP;
	cap.pmp.addr = addr;
	cap.pmp.rwx = rwx;
	cap.pmp.used = 0;
	cap.pmp.slot = 0;
	return cap;
}

cap_t cap_mk_monitor(pid_t bgn, pid_t end)
{
	cap_t cap;
	cap.mon.type = CAPTY_MONITOR;
	cap.mon.bgn = bgn;
	cap.mon.end = end;
	cap.mon.mrk = bgn;
	return cap;
}

cap_t cap_mk_channel(chan_t bgn, chan_t end)
{
	cap_t cap;
	cap.chan.type = CAPTY_CHANNEL;
	cap.chan.bgn = bgn;
	cap.chan.end = end;
	cap.chan.mrk = bgn;
	return cap;
}

cap_t cap_mk_socket(chan_t chan, ipc_mode_t mode, ipc_perm_t perm, uint32_t tag)
{
	cap_t cap;
	cap.sock.type = CAPTY_SOCKET;
	cap.sock.chan = chan;
	cap.sock.mode = mode;
	cap.sock.perm = perm;
	cap.sock.tag = tag;
	return cap;
}

cap_t cap_mk_path(uint32_t tag, path_flags_t flags)
{
	cap_t cap;
	cap.path.type = CAPTY_PATH;
	cap.path.file = flags & FILE;
	cap.path.read = flags & PATH_READ;
	cap.path.write = flags & PATH_WRITE;
	cap.path.tag = tag;
	return cap;
}

static inline bool is_range_subset(uint64_t a_bgn, uint64_t a_end, uint64_t b_bgn, uint64_t b_end)
{
	return a_bgn <= b_bgn && b_end <= a_end;
}

static inline bool is_range_prefix(uint64_t a_bgn, uint64_t a_end, uint64_t b_bgn, uint64_t b_end)
{
	return a_bgn == b_bgn && b_end <= a_end;
}

static inline bool is_bit_subset(uint64_t a, uint64_t b)
{
	return (a & b) == a;
}

static inline addr_t tag_block_to_addr(tag_t tag, block_t block)
{
	return ((uint64_t)tag << MAX_BLOCK_SIZE) + ((uint64_t)block << MIN_BLOCK_SIZE);
}

static bool cap_time_revokable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_TIME) && (p.time.hart == c.time.hart)
	       && is_range_subset(p.time.bgn, p.time.end, c.time.bgn, c.time.end);
}

static bool cap_mem_revokable(cap_t p, cap_t c)
{
	if (c.type == CAPTY_PMP) {
		uint64_t p_bgn, p_end, c_base, c_size;
		p_bgn = tag_block_to_addr(p.mem.tag, p.mem.bgn);
		p_end = tag_block_to_addr(p.mem.tag, p.mem.end);
		pmp_napot_decode(c.pmp.addr, &c_base, &c_size);
		return is_range_subset(p_bgn, p_end, c_base, c_base + c_size);
	}
	return (c.type == CAPTY_MEMORY) && (p.mem.tag == c.mem.tag)
	       && is_range_subset(p.mem.bgn, p.mem.end, c.mem.bgn, c.mem.end);
}

static bool cap_mon_revokable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_MONITOR)
	       && is_range_subset(p.mon.bgn, p.mon.end, c.mon.bgn, c.mon.end);
}

static bool cap_chan_revokable(cap_t p, cap_t c)
{
	if (c.type == CAPTY_SOCKET) {
		return is_range_subset(p.chan.bgn, p.chan.end, c.sock.chan, c.sock.chan + 1);
	}
	return (c.type == CAPTY_CHANNEL)
	       && is_range_subset(p.chan.bgn, p.chan.end, c.chan.bgn, c.chan.end);
}

static bool cap_sock_revokable(cap_t p, cap_t c)
{
	return (p.sock.tag == 0) && (c.sock.tag != 0) && (p.sock.chan == c.sock.chan);
}

// Defined in cap_fs
bool cap_path_revokable(cap_t p, cap_t c);

bool cap_is_revokable(cap_t p, cap_t c)
{
	switch (p.type) {
	case CAPTY_TIME:
		return cap_time_revokable(p, c);
	case CAPTY_MEMORY:
		return cap_mem_revokable(p, c);
	case CAPTY_MONITOR:
		return cap_mon_revokable(p, c);
	case CAPTY_CHANNEL:
		return cap_chan_revokable(p, c);
	case CAPTY_SOCKET:
		return cap_sock_revokable(p, c);
	case CAPTY_PATH:
		return cap_path_revokable(p, c);
	default:
		return false;
	}
}

bool cap_is_valid(cap_t c)
{
	switch (c.type) {
	case CAPTY_TIME:
		return (c.time.bgn == c.time.mrk) && (c.time.bgn < c.time.end);
	case CAPTY_MEMORY:
		return (c.mem.bgn == c.mem.mrk) && (c.mem.bgn < c.mem.end);
	case CAPTY_PMP:
		return (c.pmp.used == 0) && (c.pmp.slot == 0);
	case CAPTY_MONITOR:
		return (c.mon.bgn == c.mon.mrk) && (c.mon.bgn < c.mon.end);
	case CAPTY_CHANNEL:
		return (c.chan.bgn == c.chan.mrk) && (c.chan.bgn < c.chan.end);
	case CAPTY_SOCKET:
		return is_bit_subset(c.sock.perm, IPC_SDATA | IPC_CDATA | IPC_SCAP | IPC_CCAP)
		       && is_bit_subset(c.sock.mode, IPC_YIELD | IPC_NOYIELD);
	default:
		return false;
	}
}

static bool cap_time_derivable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_TIME) && (p.time.hart == c.time.hart)
	       && is_range_prefix(p.time.mrk, p.time.end, c.time.bgn, c.time.end);
}

static bool cap_mem_derivable(cap_t p, cap_t c)
{
	if (c.type == CAPTY_PMP) {
		uint64_t p_mrk, p_end, c_base, c_size;
		p_mrk = tag_block_to_addr(p.mem.tag, p.mem.mrk);
		p_end = tag_block_to_addr(p.mem.tag, p.mem.end);
		pmp_napot_decode(c.pmp.addr, &c_base, &c_size);
		return is_range_subset(p_mrk, p_end, c_base, c_base + c_size)
		       && is_bit_subset(c.pmp.rwx, p.mem.rwx);
	}
	return (c.type == CAPTY_MEMORY) && (p.mem.tag == c.mem.tag)
	       && is_range_subset(p.mem.mrk, p.mem.end, c.mem.bgn, c.mem.end)
	       && is_bit_subset(c.mem.rwx, p.mem.rwx);
}

static bool cap_mon_derivable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_MONITOR)
	       && is_range_subset(p.mon.mrk, p.mon.end, c.mon.bgn, c.mon.end);
}

static bool cap_chan_derivable(cap_t p, cap_t c)
{
	if (c.type == CAPTY_SOCKET) {
		return (c.sock.tag == 0)
		       && is_range_subset(p.chan.mrk, p.chan.end, c.sock.chan, c.sock.chan + 1);
	}
	return (c.type == CAPTY_CHANNEL)
	       && is_range_subset(p.chan.mrk, p.chan.end, c.chan.bgn, c.chan.end);
}

static bool cap_sock_derivable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_SOCKET) && (p.sock.chan == c.sock.chan) && (p.sock.tag == 0)
	       && (c.sock.tag != 0) && (p.sock.mode == c.sock.mode) && (p.sock.perm == c.sock.perm);
}

bool cap_is_derivable(cap_t p, cap_t c)
{
	switch (p.type) {
	case CAPTY_TIME:
		return cap_time_derivable(p, c);
	case CAPTY_MEMORY:
		return cap_mem_derivable(p, c);
	case CAPTY_MONITOR:
		return cap_mon_derivable(p, c);
	case CAPTY_CHANNEL:
		return cap_chan_derivable(p, c);
	case CAPTY_SOCKET:
		return cap_sock_derivable(p, c);
	default:
		return false;
	}
}
