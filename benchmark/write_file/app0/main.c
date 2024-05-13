#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/s3k.h"

#define KibiBytes(X) ((1 << 10) * (X))
// Note that large scenarios may require adjusting app0.ld to increase RAM length.
// The adjustement must be matched by the PMP setup in plat/config.h, meaning a NAPOT
// RAM offset and length should be used to match the RAM length.

#define WARMUPS 10
/* Should not matter as the result should be deterministic*/
#define MEASUREMENTS 100

#define SCENARIO_WRITE_4K_APPENDING "SCENARIO_WRITE_4K_APPENDING"
// #define SCENARIO_WRITE_4K_PARTLY_OVERLAPPING "SCENARIO_WRITE_4K_PARTLY_OVERLAPPING"
// #define SCENARIO_WRITE_4K_FULLY_OVERLAPPING \
// 	"SCENARIO_WRITE_4K_FULLY_OVERLAPPING"
#define BUF_LEN 4096

#define SCENARIO SCENARIO_WRITE_4K_APPENDING

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
				 s3k_pmp_slot_t pmp_slot,
				 s3k_napot_t napot_addr, s3k_rwx_t rwx)
{
	s3k_err_t err = S3K_SUCCESS;
	err = s3k_cap_derive(mem_cap_idx, pmp_cap_idx,
			     s3k_mk_pmp(napot_addr, rwx));
	if (err)
		return err;
	err = s3k_pmp_load(pmp_cap_idx, pmp_slot);
	if (err)
		return err;
	s3k_sync_mem();
	return err;
}

typedef struct timediff {
	uint64_t cycle;
	uint64_t mtime;
	uint64_t instret;
} timediff_t;

uint64_t csrr_cycle(void)
{
	register uint64_t cycle;
	__asm__ volatile("csrr %0, cycle" : "=r"(cycle));
	return cycle;
}

uint64_t csrr_instret(void)
{
	register uint64_t instret;
	__asm__ volatile("csrr %0, instret" : "=r"(instret));
	return instret;
}

#define ASSERT(x)                                                        \
	{                                                                \
		s3k_err_t error = (x);                                   \
		if (error) {                                             \
			alt_printf("Failed at %s:%d err=%d\n", __FILE__, \
				   __LINE__, error);                     \
			while (1)                                        \
				;                                        \
		}                                                        \
	}

uint8_t sample_buf[BUF_LEN];

timediff_t sample()
{
	timediff_t t = {0, 0, 0};

#if defined(SCENARIO_WRITE_4K_APPENDING)
#define OFFSET 4096
#elif defined(SCENARIO_WRITE_4K_PARTLY_OVERLAPPING)
#define OFFSET 2048
#elif defined(SCENARIO_WRITE_4K_FULLY_OVERLAPPING)
#define OFFSET 0
#endif
	for (size_t i = 0; i < BUF_LEN; i++) {
		sample_buf[i] = (uint8_t)i;
	}
	volatile uint32_t bw = 0;
retry:
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_instret = csrr_instret();
	uint64_t begin_cycle = csrr_cycle();
	s3k_err_t err = s3k_write_file(S3K_CAP_CNT - 1, OFFSET, sample_buf,
				       sizeof(sample_buf), &bw);
	uint64_t end_cycle = csrr_cycle();
	uint64_t end_instret = csrr_instret();
	uint64_t end_time = s3k_get_time();
	if (err == S3K_ERR_PREEMPTED) {
		goto retry;
	}
	ASSERT(err);
	// errno like assert, if true, print error message and halt
	ASSERT(!(bw == sizeof(sample_buf)));

	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	t.instret = end_instret - begin_instret;

	return t;
}

void cleanup()
{
	memset(sample_buf, 0, sizeof(sample_buf));
	// Truncate / reset by deleting the file and creating a new one
	ASSERT(s3k_path_delete(S3K_CAP_CNT - 1));
	volatile uint32_t bw = 0;
	// Write 4K to the file to allow partial overlapping writes
	ASSERT(s3k_write_file(S3K_CAP_CNT - 1, 0, sample_buf,
			      sizeof(sample_buf), &bw));
	ASSERT(!(bw == sizeof(sample_buf)));
}

uint8_t setup_buf[BUF_LEN];

void setup()
{
	ASSERT(s3k_path_derive(ROOT_PATH, "abc.txt", S3K_CAP_CNT - 1,
			       KibiBytes(8) + 32 /* CREATING DIRECTORY ENTRY included */,
				   FILE | PATH_WRITE));
}

timediff_t measurements[MEASUREMENTS];

void do_benchmark()
{
	setup();
	for (size_t i = 0; i < WARMUPS; i++) {
		sample();
		cleanup();
	}

	for (size_t i = 0; i < MEASUREMENTS; i++) {
		measurements[i] = sample();
		cleanup();
	}

	alt_puts("Scenario: " SCENARIO);

	alt_puts("cycle,mtime,instret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d\t%d\t%d\n", measurements[i].cycle,
			   measurements[i].mtime, measurements[i].instret);
	}
}

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err = setup_pmp_from_mem_cap(
	    UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	do_benchmark();

	alt_puts("Successful execution of test program");
}