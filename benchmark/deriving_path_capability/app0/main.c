#include "altc/altio.h"
#include "s3k/s3k.h"

#define WARMUPS 1
/* Should not matter as the result should be deterministic*/
#define MEASUREMENTS 2

#define REVOKE

// #define SCENARIO_SYS_CALL "SCENARIO_SYS_CALL"
// #define SCENARIO_CAP_TY_TIME "SCENARIO_CAP_TY_TIME"
// #define SCENARIO_CAP_TY_MEM "SCENARIO_CAP_TY_MEM"
#define SCENARIO_CAP_TY_MEM_INTERMEDIARY "SCENARIO_CAP_TY_MEM_INTERMEDIARY"
// #define SCENARIO_CAP_TY_PATH "SCENARIO_CAP_TY_PATH"
// #define SCENARIO_CAP_TY_PATH_INTERMEDIARY "SCENARIO_CAP_TY_PATH_INTERMEDIARY"

#define SCENARIO SCENARIO_CAP_TY_MEM_INTERMEDIARY

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

timediff_t sample_derive()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_instret = csrr_instret();
	uint64_t begin_cycle = csrr_cycle();
#if defined(SCENARIO_CAP_TY_TIME)
	ASSERT(s3k_cap_revoke(
	    HART3_TIME)); // the mrk of HART3_TIME needs to be reset
	// take new start timestamps
	begin_time = s3k_get_time();
	begin_instret = csrr_instret();
	begin_cycle = csrr_cycle();
	s3k_cap_t c = s3k_mk_time(3, 0, S3K_SLOT_CNT - 1);
	ASSERT(s3k_cap_derive(HART3_TIME, S3K_CAP_CNT - 1, c));
#elif defined(SCENARIO_CAP_TY_MEM)
	ASSERT(s3k_cap_revoke(RAM_MEM)); // the mrk of RAM_MEM needs to be reset
	// take new start timestamps
	begin_time = s3k_get_time();
	begin_instret = csrr_instret();
	begin_cycle = csrr_cycle();
	s3k_cap_t c = s3k_mk_memory(0x80210000, 0x80220000, S3K_MEM_RWX);
	ASSERT(s3k_cap_derive(RAM_MEM, S3K_CAP_CNT - 1, c));
#elif defined(SCENARIO_CAP_TY_MEM_INTERMEDIARY)
	ASSERT(s3k_cap_revoke(RAM_MEM)); // the mrk of RAM_MEM needs to be reset
	// take new start timestamps
	begin_time = s3k_get_time();
	begin_instret = csrr_instret();
	begin_cycle = csrr_cycle();
	s3k_cap_t c = s3k_mk_memory(0x80210000, 0x80220000, S3K_MEM_RWX);
	ASSERT(s3k_cap_derive(RAM_MEM, S3K_CAP_CNT - 1, c));
	s3k_cap_t c2 = c;
	for (size_t i = 0; i < 15; i++) {
		c2.mem.bgn = c.mem.bgn + i;
		c2.mem.mrk = c.mem.bgn + i;
		c2.mem.end = c.mem.bgn + i + 1;
		ASSERT(
		    s3k_cap_derive(S3K_CAP_CNT - 1, S3K_CAP_CNT - 2 - i, c2));
	}
#elif defined(SCENARIO_CAP_TY_PATH)
	ASSERT(s3k_path_derive(ROOT_PATH, "a.txt", S3K_CAP_CNT - 1,
			       FILE | PATH_READ));
#elif defined(SCENARIO_CAP_TY_PATH_INTERMEDIARY)
	ASSERT(s3k_path_derive(ROOT_PATH, "a", S3K_CAP_CNT - 1, PATH_READ));
	for (size_t i = 0; i < 15; i++) {
		ASSERT(s3k_path_derive(S3K_CAP_CNT - 1, "b.txt",
				       S3K_CAP_CNT - 2 - i, FILE | PATH_READ));
	}
#elif defined(SCENARIO_SYS_CALL)
	s3k_get_pid();
#else
#error "NO SCENARIO DEFINED"
#endif

	uint64_t end_cycle = csrr_cycle();
	uint64_t end_instret = csrr_instret();
	uint64_t end_time = s3k_get_time();
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	t.instret = end_instret - begin_instret;
	return t;
}

timediff_t sample_delete()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_instret = csrr_instret();
	uint64_t begin_cycle = csrr_cycle();
	ASSERT(s3k_cap_delete(S3K_CAP_CNT - 1));
	uint64_t end_cycle = csrr_cycle();
	uint64_t end_instret = csrr_instret();
	uint64_t end_time = s3k_get_time();
#if defined(SCENARIO_CAP_TY_PATH_INTERMEDIARY) \
    || defined(SCENARIO_CAP_TY_MEM_INTERMEDIARY)
	for (size_t i = 0; i < 15; i++) {
		ASSERT(s3k_cap_delete(S3K_CAP_CNT - 2 - i));
	}
#endif
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	t.instret = end_instret - begin_instret;
	return t;
}

timediff_t sample_revoke()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_instret = csrr_instret();
	uint64_t begin_cycle = csrr_cycle();
	ASSERT(s3k_cap_revoke(S3K_CAP_CNT - 1));
	uint64_t end_cycle = csrr_cycle();
	uint64_t end_instret = csrr_instret();
	uint64_t end_time = s3k_get_time();
	ASSERT(s3k_cap_delete(S3K_CAP_CNT - 1));
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	t.instret = end_instret - begin_instret;
	return t;
}

timediff_t sample_syscall()
{
	timediff_t t = {0, 0};
	uint64_t begin_time = s3k_get_time();
	uint64_t begin_instret = csrr_instret();
	uint64_t begin_cycle = csrr_cycle();
	s3k_get_pid();
	uint64_t end_cycle = csrr_cycle();
	uint64_t end_instret = csrr_instret();
	uint64_t end_time = s3k_get_time();
	t.cycle = end_cycle - begin_cycle;
	t.mtime = end_time - begin_time;
	t.instret = end_instret - begin_instret;
	return t;
}

timediff_t measurements_derive[MEASUREMENTS];
timediff_t measurements_delete[MEASUREMENTS];
timediff_t measurements_revoke[MEASUREMENTS];

void do_derive_revoke_delete()
{
	for (size_t i = 0; i < WARMUPS; i++) {
		sample_derive();
#if defined(REVOKE)
		sample_revoke();
#elif defined(DELETE)
		sample_delete();
#else
#error "NO OPERATION DEFINED"
#endif
	}

	for (size_t i = 0; i < MEASUREMENTS; i++) {
		measurements_derive[i] = sample_derive();
#if defined(REVOKE)
		measurements_revoke[i] = sample_revoke();
#elif defined(DELETE)
		measurements_delete[i] = sample_delete();
#else
#error "NO OPERATION DEFINED"
#endif
	}

	alt_puts("Scenario: " SCENARIO);

#if defined(REVOKE)
	alt_puts(
	    "measurements_derive_cycle,measurements_derive_mtime,measurements_derive_instret,measurements_revoke_cycle,measurements_revoke_mtime,measurements_revoke_instret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d\t%d\t%d,%d\t%d\t%d\n", measurements_derive[i].cycle,
			   measurements_derive[i].mtime,
			   measurements_derive[i].instret,
			   measurements_revoke[i].cycle,
			   measurements_revoke[i].mtime,
			   measurements_revoke[i].instret);
	}
#elif defined(DELETE)
	alt_puts(
	    "measurements_derive_cycle,measurements_derive_mtime,measurements_derive_instret,measurements_delete_cycle,measurements_delete_mtime,measurements_delete_instret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d\t%d\t%d,%d\t%d\t%d\n", measurements_derive[i].cycle,
			   measurements_derive[i].mtime,
			   measurements_derive[i].instret,
			   measurements_delete[i].cycle,
			   measurements_delete[i].mtime,
			   measurements_delete[i].instret);
	}
#endif
}

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err = setup_pmp_from_mem_cap(
	    UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

#if !defined(SCENARIO_SYS_CALL)
	do_derive_revoke_delete();
#else
	for (size_t i = 0; i < WARMUPS; i++) {
		sample_syscall();
	}

	for (size_t i = 0; i < MEASUREMENTS; i++) {
		measurements_derive[i] = sample_syscall();
	}
	alt_puts("Scenario: " SCENARIO);
	alt_puts("measurements_cycle,measurements_mtime,measurements_instret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d\t%d\t%d\n", measurements_derive[i].cycle,
			   measurements_derive[i].mtime,
			   measurements_derive[i].instret);
	}
#endif

	alt_puts("Successful execution of test program");
}