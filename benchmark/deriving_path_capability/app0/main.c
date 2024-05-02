#include "altc/altio.h"
#include "s3k/s3k.h"

#define WARMUPS 1
/* Should not matter as the result should be deterministic*/
#define MEASUREMENTS 2

#define SCENARIO_CAP_TY_TIME "SCENARIO_CAP_TY_TIME"
// #define SCENARIO_CAP_TY_PATH "SCENARIO_CAP_TY_PATH"

#define SCENARIO SCENARIO_CAP_TY_TIME

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
} timediff_t;

uint64_t csrr_cycle(void)
{
	register uint64_t cycle;
	__asm__ volatile("csrr %0, cycle" : "=r"(cycle));
	return cycle;
}

#define ASSERT(x)                                                      \
	{                                                              \
		s3k_err_t err = (x);                                   \
		if (err) {                                             \
			alt_printf("Failed at %s: err=%d\n", #x, err); \
			while (1)                                      \
				;                                      \
		}                                                      \
	}

timediff_t sample_derive()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_cycle = csrr_cycle();
#if defined(SCENARIO_CAP_TY_TIME)
	s3k_cap_t c = s3k_mk_time(3, 0, S3K_SLOT_CNT - 1);
	s3k_err_t err = s3k_cap_derive(HART3_TIME, S3K_CAP_CNT - 1, c);
	ASSERT(err);
#elif defined(SCENARIO_CAP_TY_PATH)
	s3k_err_t err = s3k_path_derive(ROOT_PATH, "a.txt", S3K_CAP_CNT - 1,
					FILE | PATH_READ);
	ASSERT(err);
#else
#error "NO SCENARIO DEFINED"
#endif

	uint64_t end_cycle = csrr_cycle();
	uint64_t end_time = s3k_get_time();
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	return t;
}

timediff_t sample_delete()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_cycle = csrr_cycle();
	s3k_err_t err = s3k_cap_delete(S3K_CAP_CNT - 1);
	ASSERT(err);
	uint64_t end_cycle = csrr_cycle();
	uint64_t end_time = s3k_get_time();
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	return t;
}

timediff_t measurements_derive[MEASUREMENTS];
timediff_t measurements_delete[MEASUREMENTS];

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err = setup_pmp_from_mem_cap(
	    UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	for (size_t i = 0; i < WARMUPS; i++) {
		sample_derive();
		sample_delete();
	}

	for (size_t i = 0; i < MEASUREMENTS; i++) {
		measurements_derive[i] = sample_derive();
		measurements_delete[i] = sample_delete();
	}

	alt_puts("Scenario: " SCENARIO);

	alt_puts(
	    "measurements_derive_cycle,measurements_derive_mtime,measurements_delete_cycle,measurements_delete_mtime");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d,%d,%d,%d\n", measurements_derive[i].cycle,
			   measurements_derive[i].mtime,
			   measurements_delete[i].cycle,
			   measurements_delete[i].mtime);
	}

	alt_puts("Successful execution of test program");
}