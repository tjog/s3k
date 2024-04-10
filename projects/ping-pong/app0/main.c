#include "altc/altio.h"
#include "s3k/s3k.h"
#include "string.h"

#define APP0_PID 0
#define APP1_PID 1

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

#define SUCCESS_OR_RETURN_ERR(x)    \
	do {                        \
		err = x;            \
		if (err)            \
			return err; \
	} while (false);

s3k_err_t setup_uart(uint64_t uart_idx)
{
	uint64_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err = S3K_SUCCESS;
	// Derive a PMP capability for accessing UART
	SUCCESS_OR_RETURN_ERR(
	    s3k_cap_derive(UART_MEM, uart_idx, s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
	// Load the derive PMP capability to PMP configuration
	SUCCESS_OR_RETURN_ERR(s3k_pmp_load(uart_idx, 1));
	// Synchronize PMP unit (hardware) with PMP configuration
	s3k_sync_mem();
	return err;
}

s3k_err_t setup_app1(uint64_t tmp)
{
	uint64_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	uint64_t app1_addr = s3k_napot_encode(0x80020000, 0x10000);
	s3k_err_t err = S3K_SUCCESS;

	// Derive a PMP capability for app1 main memory
	SUCCESS_OR_RETURN_ERR(s3k_cap_derive(RAM_MEM, tmp, s3k_mk_pmp(app1_addr, S3K_MEM_RWX)));
	SUCCESS_OR_RETURN_ERR(s3k_mon_cap_move(MONITOR, APP0_PID, tmp, APP1_PID, 0));
	SUCCESS_OR_RETURN_ERR(s3k_mon_pmp_load(MONITOR, APP1_PID, 0, 0));

	// Derive a PMP capability for uart
	SUCCESS_OR_RETURN_ERR(s3k_cap_derive(UART_MEM, tmp, s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
	SUCCESS_OR_RETURN_ERR(s3k_mon_cap_move(MONITOR, APP0_PID, tmp, APP1_PID, 1));
	SUCCESS_OR_RETURN_ERR(s3k_mon_pmp_load(MONITOR, APP1_PID, 1, 1));

	// derive a time slice capability
	// s3k_cap_derive(HART0_TIME, tmp, s3k_mk_time(S3K_MIN_HART, 0,
	// S3K_SLOT_CNT / 2));
	SUCCESS_OR_RETURN_ERR(s3k_mon_cap_move(MONITOR, APP0_PID, HART1_TIME, APP1_PID, 2));

	// Write start PC of app1 to PC
	SUCCESS_OR_RETURN_ERR(s3k_mon_reg_write(MONITOR, APP1_PID, S3K_REG_PC, 0x80020000));
	return err;
}

s3k_err_t setup_socket(uint64_t socket, uint64_t tmp)
{
	s3k_err_t err = S3K_SUCCESS;

	SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
	    CHANNEL, socket, s3k_mk_socket(0, S3K_IPC_YIELD, S3K_IPC_SDATA | S3K_IPC_CDATA, 0)));
	SUCCESS_OR_RETURN_ERR(s3k_cap_derive(
	    socket, tmp, s3k_mk_socket(0, S3K_IPC_YIELD, S3K_IPC_SDATA | S3K_IPC_CDATA, 1)));
	SUCCESS_OR_RETURN_ERR(s3k_mon_cap_move(MONITOR, APP0_PID, tmp, APP1_PID, 3));
	return err;
}

int main(void)
{
	s3k_err_t err = S3K_SUCCESS;
	// Setup UART access
	err = setup_uart(13);
	if (err) {
		alt_puts("setup uart failed");
	}

	alt_puts("starting app0");

	// Setup app1 capabilities and PC
	err = setup_app1(14);
	if (err) {
		alt_puts("setup app1 failed");
	}

	// Setup socket capabilities.
	err = setup_socket(14, 15);
	if (err) {
		alt_puts("setup socket failed");
	}

	// Resume app1
	s3k_mon_resume(MONITOR, APP1_PID);

	s3k_msg_t msg;
	s3k_reply_t reply;
	memcpy(msg.data, "pong", 5);
	s3k_reg_write(S3K_REG_SERVTIME, 1500);
	while (1) {
		do {
			reply = s3k_sock_sendrecv(14, &msg);
		} while (reply.err);
		alt_puts((char *)reply.data);
	}
}
