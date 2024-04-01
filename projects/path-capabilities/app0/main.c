#include "altc/altio.h"
#include "s3k/s3k.h"

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

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err
	    = setup_pmp_from_mem_cap(UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	err = s3k_path_derive(ROOT_PATH, "newfile.txt", newfile_PATH, FILE | PATH_READ);
	if (err) {
		alt_printf("Error from path derive: 0x%X", err);
		return -1;
	}
	err = s3k_path_derive(ROOT_PATH, "newdir", newdir_PATH, PATH_READ | PATH_WRITE);
	if (err) {
		alt_printf("Error from path derive: 0x%X", err);
		return -1;
	}
	err = s3k_path_derive(newdir_PATH, "nested.txt", nested_PATH, PATH_READ | PATH_WRITE);
	if (err) {
		alt_printf("Error from path derive: 0x%X", err);
		return -1;
	}

	for (size_t i = 0; i < 16; i++) {
		alt_printf("Capability %d: ", i);
		s3k_cap_t cap;
		s3k_err_t err = s3k_cap_read(i, &cap);
		if (!err) {
			print_cap(cap);
		} else {
			alt_putstr("NONE");
		}
		if (cap.type == S3K_CAPTY_PATH) {
			char buf[50];
			s3k_err_t err = s3k_path_read(i, buf, 50);
			if (!err) {
				alt_putstr(" (='");
				alt_putstr(buf);
				alt_putstr("')");
			}
		}
		alt_putchar('\n');
	}

	char b[] = "Hello";
	uint32_t res = 0;
	err = s3k_write_file(nested_PATH, 0, b, sizeof(b), &res);
	if (err) {
		alt_printf("Error from s3k_write_file: %d", err);
		return -1;
	}
}