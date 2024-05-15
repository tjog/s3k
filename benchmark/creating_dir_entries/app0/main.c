#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/s3k.h"

#define WARMUPS 10
/* Should not matter as the result should be deterministic*/
#define MEASUREMENTS 100

#define SCENARIO_DFS_WIDTH_1_DEPTH_24 "SCENARIO_DFS_WIDTH_1_DEPTH_24"
// #define SCENARIO_DFS_WIDTH_4_DEPTH_4 "SCENARIO_DFS_WIDTH_4_DEPTH_4"
// #define SCENARIO_DFS_WIDTH_3_DEPTH_5 "SCENARIO_DFS_WIDTH_3_DEPTH_5"

#define SCENARIO SCENARIO_DFS_WIDTH_1_DEPTH_24

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

char path[100];

#define MibiBytes(X) ((1 << 20) * (X))


static void iterate_paths(char *dest, int width, int depth, timediff_t *t)
{
	if (depth == 0)
		return;

	for (char c = 'a'; c < 'a' + width; c++) {
		dest[0] = c;
		dest[1] = '/';
		dest[2] = '\0';
	retry2:
		s3k_err_t err
		    = (s3k_path_derive(ROOT_PATH, path, S3K_CAP_CNT - 1,
				       MibiBytes(1), PATH_WRITE));
		if (err == S3K_ERR_PREEMPTED) {
			goto retry2;
		}
		ASSERT(err);
	retry:
		uint64_t begin_time = s3k_get_time();
		uint64_t begin_instret = csrr_instret();
		uint64_t begin_cycle = csrr_cycle();
		err = s3k_create_dir(S3K_CAP_CNT - 1, true);
		uint64_t end_cycle = csrr_cycle();
		uint64_t end_instret = csrr_instret();
		uint64_t end_time = s3k_get_time();
		if (err == S3K_ERR_PREEMPTED) {
			goto retry;
		}
		ASSERT(err);
		t->cycle += end_cycle - begin_cycle;
		t->mtime += end_time - begin_time;
		t->instret += end_instret - begin_instret;
		ASSERT(s3k_cap_delete(S3K_CAP_CNT - 1));

		iterate_paths(dest + 2, width, depth - 1, t);
	}
}

timediff_t sample()
{
	timediff_t t = {0, 0, 0};

#if defined(SCENARIO_DFS_WIDTH_1_DEPTH_24)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths(path, 1, 24, &t);
#elif defined(SCENARIO_DFS_WIDTH_4_DEPTH_4)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths(path, 4, 4, &t);
#elif defined(SCENARIO_DFS_WIDTH_3_DEPTH_5)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths(path, 3, 5, &t);
#else
#error "NO SCENARIO DEFINED"
#endif

	return t;
}

static void iterate_paths_cleanup(char *dest, int width, int depth)
{
	if (depth == 0)
		return;

	for (char c = 'a'; c < 'a' + width; c++) {
		dest[0] = c;
		dest[1] = '/';
		dest[2] = '\0';
		iterate_paths_cleanup(dest + 2, width, depth - 1);
		dest[2] = '\0';
	retry2:
		s3k_err_t err
		    = (s3k_path_derive(ROOT_PATH, path, S3K_CAP_CNT - 1,
				       MibiBytes(1), PATH_WRITE));
		if (err == S3K_ERR_PREEMPTED) {
			goto retry2;
		}
		ASSERT(err);
	retry:
		err = s3k_path_delete(S3K_CAP_CNT - 1);
		if (err == S3K_ERR_PREEMPTED) {
			goto retry;
		}
		ASSERT(err);
		ASSERT(s3k_cap_delete(S3K_CAP_CNT - 1));
	}
}

void cleanup()
{
#if defined(SCENARIO_DFS_WIDTH_1_DEPTH_24)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths_cleanup(path, 1, 24);
#elif defined(SCENARIO_DFS_WIDTH_4_DEPTH_4)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths_cleanup(path, 4, 4);
#elif defined(SCENARIO_DFS_WIDTH_3_DEPTH_5)
	memset(path, 0, sizeof(path));
	path[0] = '/';
	iterate_paths_cleanup(path, 3, 5);
#else
#error "NO SCENARIO DEFINED"
#endif
}

timediff_t measurements[MEASUREMENTS];

void do_benchmark()
{
	for (size_t i = 0; i < WARMUPS; i++) {
		sample();
		cleanup();
	}

	for (size_t i = 0; i < MEASUREMENTS; i++) {
		measurements[i] = sample();
		alt_puts("Finished sample");
		cleanup();
	}

	alt_puts("Scenario: " SCENARIO);

	alt_puts("cycle,mtime,instret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d,%d,%d\n", measurements[i].cycle,
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