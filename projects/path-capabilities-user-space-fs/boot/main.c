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
#define newfile_PATH 13
#define newdir_PATH 14
#define nested_PATH 15

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


#define SUCCESS_OR_RETURN_ERR(x)    \
	do {                        \
		err = x;            \
		if (err)            \
			return err; \
	} while (false);

s3k_err_t setup_fs()
{
	s3k_cidx_t boot_tmp = S3K_CAP_CNT - 1;
	s3k_cidx_t next_fs_cidx = 0;
	s3k_pmp_slot_t next_fs_pmp = 0;
	s3k_err_t err = S3K_SUCCESS;

	// MEMORY+PMP
	{
		SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
		    RAM_MEM, boot_tmp,
		    s3k_mk_memory((uint64_t)FS_MEM, (uint64_t)FS_MEM + FS_MEM_LEN, S3K_MEM_RWX)));
		s3k_napot_t fs_mem_addr = s3k_napot_encode((uint64_t)FS_MEM, FS_MEM_LEN);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(boot_tmp, boot_tmp - 1, s3k_mk_pmp(fs_mem_addr, S3K_MEM_RWX)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID, next_fs_cidx));
		next_fs_cidx++;
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp - 1, FS_PID, next_fs_cidx));
		SUCCESS_OR_RETURN_ERR(s3k_mon_pmp_load(MONITOR, FS_PID, next_fs_cidx, next_fs_pmp));
		next_fs_cidx++;
		next_fs_pmp++;
	}

	// Derive a PMP capability for uart (to allow debug output)
	{
		s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(UART_MEM, boot_tmp, s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID, next_fs_cidx));
		SUCCESS_OR_RETURN_ERR(s3k_mon_pmp_load(MONITOR, FS_PID, next_fs_cidx, next_fs_pmp));
		next_fs_cidx++;
		next_fs_pmp++;
	}

	// VIRTIO
	{
		s3k_napot_t virtio_addr = s3k_napot_encode(VIRTIO0_BASE_ADDR, 0x1000);
		SUCCESS_OR_RETURN_ERR(
		    s3k_cap_derive(VIRTIO, boot_tmp, s3k_mk_pmp(virtio_addr, S3K_MEM_RW)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID, next_fs_cidx));
		SUCCESS_OR_RETURN_ERR(s3k_mon_pmp_load(MONITOR, FS_PID, next_fs_cidx, next_fs_pmp));
		next_fs_cidx++;
		next_fs_pmp++;
	}

	// SERVER SOCKET
	{
		SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
		    CHANNEL, boot_tmp,
		    s3k_mk_socket(FS_CHANNEL, S3K_IPC_NOYIELD,
				  S3K_IPC_CCAP | S3K_IPC_CDATA | S3K_IPC_SCAP | S3K_IPC_SDATA, 0)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID, next_fs_cidx));
		next_fs_cidx++;
	}

	// TIME
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, HART1_TIME, FS_PID, next_fs_cidx));
		next_fs_cidx++;
	}

	// Write start PC of FS to PC
	SUCCESS_OR_RETURN_ERR(s3k_mon_reg_write(MONITOR, FS_PID, S3K_REG_PC, (uint64_t)FS_MEM));

	return err;
}

s3k_err_t setup_app(s3k_pid_t fs_pid, uint32_t fs_client_tag)
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

	// FILE SYSTEM CLIENT SOCKET
	{
		s3k_cidx_t server_cidx = 0;
		for (size_t i = 0; i < S3K_CAP_CNT; i++) {
			s3k_cap_t cap;
			s3k_err_t err = s3k_mon_cap_read(MONITOR, FS_PID, i, &cap);
			if (err)
				continue;
			if (cap.type == S3K_CAPTY_SOCKET && cap.sock.chan == FS_CHANNEL) {
				server_cidx = i;
				break;
			}
		}
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, FS_PID, server_cidx, BOOT_PID, boot_tmp));
		SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
		    boot_tmp, boot_tmp - 1,
		    s3k_mk_socket(FS_CHANNEL, S3K_IPC_NOYIELD,
				  S3K_IPC_CCAP | S3K_IPC_CDATA | S3K_IPC_SCAP | S3K_IPC_SDATA,
				  fs_client_tag)));
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID, server_cidx));
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
	{
		SUCCESS_OR_RETURN_ERR(
		    s3k_mon_cap_move(MONITOR, BOOT_PID, HART2_TIME, APP_PID, next_app_cidx));
		next_app_cidx++;
	}

	// Write start PC of FS to PC
	SUCCESS_OR_RETURN_ERR(s3k_mon_reg_write(MONITOR, APP_PID, S3K_REG_PC, (uint64_t)APP_MEM));

	return err;
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

	err = s3k_mon_suspend(MONITOR, FS_PID);
	if (err)
		alt_printf("s3k_mon_suspend error code: %x\n", err);
	err = s3k_mon_suspend(MONITOR, APP_PID);
	if (err)
		alt_printf("s3k_mon_suspend error code: %x\n", err);

	err = setup_fs();
	if (err) {
		alt_printf("setup_fs error code: %x\n", err);
		return -1;
	}
	err = setup_app(FS_PID, 1);
	if (err) {
		alt_printf("setup_app error code: %x\n", err);
		return -1;
	}

	mon_dump_caps_range(BOOT_PID, MONITOR, BOOT_PID, 0, S3K_CAP_CNT - 1);
	mon_dump_caps_range(BOOT_PID, MONITOR, FS_PID, 0, S3K_CAP_CNT - 1);
	mon_dump_caps_range(BOOT_PID, MONITOR, APP_PID, 0, S3K_CAP_CNT - 1);

	// mon_dump_caps_range(MONITOR, BOOT_PID, 0, 10);
	// mon_dump_caps_range(MONITOR, FS_PID, 0, 5);
	// mon_dump_caps_range(MONITOR, APP_PID, 0, 5);

	alt_printf("S3K_SCHED_TIME = %d\n", S3K_SCHED_TIME);

	alt_puts("Successful execution of boot program");

	err = s3k_mon_resume(MONITOR, FS_PID);
	if (err)
		alt_printf("s3k_mon_resume error code: %x\n", err);

	err = s3k_mon_resume(MONITOR, APP_PID);
	if (err)
		alt_printf("s3k_mon_resume error code: %x\n", err);
}