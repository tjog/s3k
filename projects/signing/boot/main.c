#include "../config.h"
#include "altc/altio.h"
#include "s3k/s3k.h"

// See plat_conf.h
#define BOOT_PMP 0
#define RAM_MEM 1
#define UART_MEM 2
#define TIME_MEM 3
#define HART0_TIME 4
#define HART1_TIME 5
#define HART2_TIME 6
#define HART3_TIME 7
#define MONITOR 8
#define CHANNEL 9
#define VIRTIO 10
#define ROOT_PATH 11

// Derived
#define UART_PMP 12
#define UART_PMP_SLOT 1

s3k_err_t setup_pmp_from_mem_cap(s3k_cidx_t mem_cap_idx, s3k_cidx_t pmp_cap_idx,
				 s3k_pmp_slot_t pmp_slot, s3k_napot_t napot_addr, s3k_rwx_t rwx)
{
	s3k_err_t err = S3K_SUCCESS;
	err = s3k_cap_derive(mem_cap_idx, pmp_cap_idx, s3k_mk_pmp(napot_addr, rwx));
	if (err)
		return err;
	err = s3k_pmp_load(pmp_cap_idx, pmp_slot);
	if (err)
		return err;
	s3k_sync_mem();
	return err;
}

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

void mon_dump_caps_range(s3k_cidx_t mon_idx, s3k_pid_t pid, s3k_cidx_t start, s3k_cidx_t end)
{
	alt_printf("Dumping caps for PID %d for idx [%d,%d]:\n", pid, start, end);
	for (size_t i = start; i <= end; i++) {
		alt_printf("%d: ", i);
		s3k_cap_t cap;
		s3k_err_t err = (pid == BOOT_PID) ? s3k_cap_read(i, &cap) :
						    s3k_mon_cap_read(mon_idx, pid, i, &cap);
		if (!err) {
			print_cap(cap);
			if (cap.type == S3K_CAPTY_PATH) {
				char buf[50];
				s3k_err_t err = (pid == BOOT_PID) ?
						    s3k_path_read(i, buf, 50) :
						    s3k_mon_path_read(mon_idx, pid, i, buf, 50);
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

#define SUCCESS_OR_RETURN_ERR(x)    \
	do {                        \
		err = x;            \
		if (err)            \
			return err; \
	} while (false);

s3k_err_t setup_sign()
{
	s3k_cidx_t boot_tmp = S3K_CAP_CNT - 1;
	s3k_cidx_t next_sign_cidx = 0;
	s3k_pmp_slot_t next_sign_pmp = 0;
	s3k_err_t err = S3K_SUCCESS;

	// MEMORY+PMP
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(RAM_MEM, boot_tmp,
				   s3k_mk_memory((uint64_t)SIGN_MEM,
						 (uint64_t)SIGN_MEM + SIGN_MEM_LEN, S3K_MEM_RWX)));
		s3k_napot_t sign_mem_addr = s3k_napot_encode((uint64_t)SIGN_MEM, SIGN_MEM_LEN);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(boot_tmp, boot_tmp - 1, s3k_mk_pmp(sign_mem_addr, S3K_MEM_RWX)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, SIGN_PID, next_sign_cidx));
		next_sign_cidx++;
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp - 1, SIGN_PID, next_sign_cidx));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_pmp_load(MONITOR, SIGN_PID, next_sign_cidx, next_sign_pmp));
		next_sign_cidx++;
		next_sign_pmp++;
	}

	// Derive a PMP capability for uart (to allow debug output)
	{
		s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(UART_MEM, boot_tmp, s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, SIGN_PID, next_sign_cidx));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_pmp_load(MONITOR, SIGN_PID, next_sign_cidx, next_sign_pmp));
		next_sign_cidx++;
		next_sign_pmp++;
	}

	// SERVER SOCKET
	{
		SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
		    CHANNEL, boot_tmp,
		    s3k_mk_socket(SIGN_CHANNEL, S3K_IPC_YIELD,
				  S3K_IPC_CCAP | S3K_IPC_CDATA | S3K_IPC_SCAP | S3K_IPC_SDATA, 0)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, SIGN_PID, next_sign_cidx));
		next_sign_cidx++;
	}

	// PATH capability / "working directory"
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_path_derive(ROOT_PATH, "sign", boot_tmp, PATH_READ | PATH_WRITE));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, SIGN_PID, next_sign_cidx));
		next_sign_cidx++;
	}

	// TIME
	// {
	// 	SUCCESS_OR_RETURN_ERR(
	// 	    s3k_mon_cap_move(MONITOR, BOOT_PID, HART1_TIME, SIGN_PID, next_sign_cidx));
	// 	next_sign_cidx++;
	// }

	// Write start PC of FS to PC
	SUCCESS_OR_RETURN_ERR(s3k_mon_reg_write(MONITOR, SIGN_PID, S3K_REG_PC, (uint64_t)SIGN_MEM));

	return err;
}

s3k_err_t setup_app(s3k_pid_t sign_pid, uint32_t sign_client_tag)
{
	s3k_cidx_t boot_tmp = S3K_CAP_CNT - 1;
	s3k_cidx_t next_app_cidx = 0;
	s3k_pmp_slot_t next_app_pmp = 0;
	s3k_err_t err = S3K_SUCCESS;

	// MEMORY+PMP
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(RAM_MEM, boot_tmp,
				   s3k_mk_memory((uint64_t)APP_MEM, (uint64_t)APP_MEM + APP_MEM_LEN,
						 S3K_MEM_RWX)));
		s3k_napot_t app_mem_addr = s3k_napot_encode((uint64_t)APP_MEM, APP_MEM_LEN);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(boot_tmp, boot_tmp - 1, s3k_mk_pmp(app_mem_addr, S3K_MEM_RWX)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, APP_PID, next_app_cidx));
		next_app_cidx++;
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp - 1, APP_PID, next_app_cidx));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_pmp_load(MONITOR, APP_PID, next_app_cidx, next_app_pmp));
		next_app_cidx++;
		next_app_pmp++;
	}

	// Derive a PMP capability for uart (to allow debug output)
	{
		s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(UART_MEM, boot_tmp, s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, APP_PID, next_app_cidx));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_pmp_load(MONITOR, APP_PID, next_app_cidx, next_app_pmp));
		next_app_cidx++;
		next_app_pmp++;
	}

	// SIGN CLIENT SOCKET
	{
		s3k_cidx_t server_cidx = 0;
		for (size_t i = 0; i < S3K_CAP_CNT; i++) {
			s3k_cap_t cap;
			s3k_err_t err = s3k_mon_cap_read(MONITOR, SIGN_PID, i, &cap);
			if (err)
				continue;
			if (cap.type == S3K_CAPTY_SOCKET && cap.sock.chan == SIGN_CHANNEL) {
				server_cidx = i;
				break;
			}
		}
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, SIGN_PID, server_cidx, BOOT_PID, boot_tmp));
		SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
		    boot_tmp, boot_tmp - 1,
		    s3k_mk_socket(SIGN_CHANNEL, S3K_IPC_YIELD,
				  S3K_IPC_CCAP | S3K_IPC_CDATA | S3K_IPC_SCAP | S3K_IPC_SDATA,
				  sign_client_tag)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, SIGN_PID, server_cidx));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp - 1, APP_PID, next_app_cidx));
		next_app_cidx++;
	}

	// PATH capability / "working directory"
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_path_derive(ROOT_PATH, "app", boot_tmp, PATH_READ | PATH_WRITE));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, APP_PID, next_app_cidx));
		next_app_cidx++;
	}

	// TIME
	// {
	// 	SUCCESS_OR_RETURN_ERR(
	// 	    s3k_mon_cap_move(MONITOR, BOOT_PID, HART2_TIME, APP_PID, next_app_cidx));
	// 	next_app_cidx++;
	// }

	// Write start PC of APP to PC
	SUCCESS_OR_RETURN_ERR(s3k_mon_reg_write(MONITOR, APP_PID, S3K_REG_PC, (uint64_t)APP_MEM));

	return err;
}

s3k_cidx_t mon_find_free_cidx(s3k_cidx_t mon_idx, s3k_pid_t pid)
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_mon_cap_read(mon_idx, pid, i, &c);
		if (err == S3K_ERR_EMPTY)
			return i;
		if (err)
			alt_printf("s3k_mon_cap_read error code: %x\n", err);
	}
	return S3K_CAP_CNT;
}

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err
	    = setup_pmp_from_mem_cap(UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	// mon_dump_caps_range(MONITOR, BOOT_PID, 0, 15);

	err = s3k_mon_suspend(MONITOR, SIGN_PID);
	if (err)
		alt_printf("s3k_mon_suspend error code: %x\n", err);
	err = s3k_mon_suspend(MONITOR, APP_PID);
	if (err)
		alt_printf("s3k_mon_suspend error code: %x\n", err);

	err = setup_sign();
	if (err) {
		alt_printf("setup_fs error code: %x\n", err);
		return -1;
	}
	err = setup_app(SIGN_PID, 1);
	if (err) {
		alt_printf("setup_app error code: %x\n", err);
		return -1;
	}

	mon_dump_caps_range(MONITOR, BOOT_PID, 0, S3K_CAP_CNT - 1);
	mon_dump_caps_range(MONITOR, SIGN_PID, 0, S3K_CAP_CNT - 1);
	mon_dump_caps_range(MONITOR, APP_PID, 0, S3K_CAP_CNT - 1);

	// mon_dump_caps_range(MONITOR, BOOT_PID, 0, 10);
	// mon_dump_caps_range(MONITOR, SIGN_PID, 0, 5);
	// mon_dump_caps_range(MONITOR, APP_PID, 0, 5);

	alt_printf("S3K_SCHED_TIME = %d\n", S3K_SCHED_TIME);

	err = s3k_mon_resume(MONITOR, SIGN_PID);
	if (err) {
		alt_printf("s3k_mon_resume error code: %x\n", err);
		return -1;
	}

	// Let SIGN intialize, i.e. wait until it is blocking on receving IPC
	s3k_state_t state = 0;
	do {
		err = s3k_mon_yield(MONITOR, SIGN_PID);
		if (err)
			alt_printf("s3k_mon_yield error code: %x\n", err);
		err = s3k_mon_state_get(MONITOR, SIGN_PID, &state);
		if (err)
			alt_printf("s3k_mon_state_get error code: %x\n", err);
	} while (err || state != S3K_PSF_BLOCKED);

	alt_puts("Boot program completed wait on SIGN blocking on IPC");

	s3k_cidx_t free_app_cidx = mon_find_free_cidx(MONITOR, APP_PID);
	if (free_app_cidx == S3K_CAP_CNT) {
		alt_puts("mon_find_free_cidx did not succeed");
		return -1;
	}

	// TIME
	{
		// mon_dump_caps_range(MONITOR, APP_PID, 0, S3K_CAP_CNT - 1);
		err = s3k_mon_cap_move(MONITOR, BOOT_PID, HART1_TIME, APP_PID, free_app_cidx);
		if (err)
			alt_printf("s3k_mon_cap_move error code: %x\n", err);
		// mon_dump_caps_range(MONITOR, APP_PID, 0, S3K_CAP_CNT - 1);

		// err = s3k_mon_yield(MONITOR, APP_PID);
		// if (err)
		// 	alt_printf("s3k_mon_yield error code: %x\n", err);
	}
	alt_puts("Boot program completed cap move to APP");

	err = s3k_mon_resume(MONITOR, APP_PID);
	if (err) {
		alt_printf("s3k_mon_resume error code: %x\n", err);
		return -1;
	}

	alt_puts("Boot program completed everything");
}