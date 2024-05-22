#include "../config.h"
#include "altc/altio.h"
#include "altc/string.h"
#include "s3k/fs.h"
#include "s3k/s3k.h"

#define BUFFER_TIME 2000000

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

static uint8_t storage[4096];
static s3k_cidx_t client_cidx;

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

#define STATE_WHEN_BLOCKING_ON_CHANNEL(chan) \
	(S3K_PSF_BLOCKED | ((uint64_t)chan << 32))

void wait_for_fs_ready()
{
	s3k_err_t err = S3K_SUCCESS;
	s3k_state_t state = 0;
	/* Wait until we are blocking on FS_CHANNEL */
	ASSERT(s3k_mon_state_get(MONITOR, FS_PID, &state));
	while (state != STATE_WHEN_BLOCKING_ON_CHANNEL(FS_CHANNEL)) {
		ASSERT(s3k_mon_yield(MONITOR, FS_PID));
		ASSERT(s3k_mon_state_get(MONITOR, FS_PID, &state));
	};
	alt_puts("FS is ready");
}

char path[100];

static void iterate_paths(char *dest, int width, int depth, timediff_t *t)
{
	if (depth == 0)
		return;

	for (char c = 'a'; c < 'a' + width; c++) {
		dest[0] = c;
		dest[1] = '/';
		dest[2] = '\0';
	retry2:
		s3k_err_t err = (s3k_path_derive(ROOT_PATH, path,
						 S3K_CAP_CNT - 1, PATH_WRITE));
		if (err == S3K_ERR_PREEMPTED) {
			goto retry2;
		}
		ASSERT(err);
		const s3k_msg_t msg = {
		    .cap_idx = S3K_CAP_CNT - 1,
		    .send_cap = true,
		    .data = {
			     [0] = fs_create_dir,
			     [1] = true,
			     [2] = 0,
			     [3] = 0,
			     }
		     };
	retry:

		uint64_t begin_time = s3k_get_time();
		uint64_t timeout = s3k_get_timeout();
		// Only try when we have a big amount of time left
		if (timeout - begin_time < BUFFER_TIME) {
			goto retry;
		}
		uint64_t begin_instret = csrr_instret();
		uint64_t begin_cycle = csrr_cycle();
		s3k_reply_t reply = s3k_try_sock_sendrecv(client_cidx, &msg);
		uint64_t end_cycle = csrr_cycle();
		uint64_t end_instret = csrr_instret();
		uint64_t end_time = s3k_get_time();
		if (reply.err == S3K_ERR_PREEMPTED) {
			goto retry;
		}
		ASSERT(reply.err);
		ASSERT(reply.data[0]);
		t->cycle += end_cycle - begin_cycle;
		t->mtime += end_time - begin_time;
		t->instret += end_instret - begin_instret;

		// wait_for_fs_ready();

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
		s3k_err_t err = (s3k_path_derive(ROOT_PATH, path,
						 S3K_CAP_CNT - 1, PATH_WRITE));
		if (err == S3K_ERR_PREEMPTED) {
			goto retry2;
		}
		ASSERT(err);
	retry:
		uint64_t begin_time = s3k_get_time();
		uint64_t timeout = s3k_get_timeout();
		// Only try when we have a big amount of time left
		if (timeout - begin_time < BUFFER_TIME) {
			goto retry;
		}
		s3k_reply_t reply
		    = send_fs_delete_entry(client_cidx, S3K_CAP_CNT - 1);
		if (reply.err == S3K_ERR_PREEMPTED) {
			goto retry;
		}
		ASSERT(reply.err);
		ASSERT(reply.data[0]);
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
		cleanup();
	}

	alt_puts("Scenario: " SCENARIO);

	alt_puts("cycle\ttime\tinstret");
	for (size_t i = 0; i < MEASUREMENTS; i++) {
		alt_printf("%d\t%d\t%d\n", measurements[i].cycle,
			   measurements[i].mtime, measurements[i].instret);
	}
}

static s3k_cidx_t setup_fs_caps();
static s3k_cidx_t find_free_cidx();

int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err = setup_pmp_from_mem_cap(
	    UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	// BOOT MEM IS BEFORE FS MEM, so deriving it from RAM_MEM must be first
	s3k_cidx_t free_cidx = find_free_cidx();
	ASSERT(s3k_cap_derive(RAM_MEM, free_cidx,
			      s3k_mk_memory((uint64_t)BOOT_MEM,
					    (uint64_t)BOOT_MEM + BOOT_LEN,
					    S3K_MEM_RWX)));
	s3k_cidx_t self_mem_cidx = free_cidx;
	free_cidx = find_free_cidx();
	ASSERT(free_cidx == S3K_CAP_CNT);

	client_cidx = setup_fs_caps();

	mon_dump_caps_range(BOOT_PID, MONITOR, BOOT_PID, 0, S3K_CAP_CNT - 1);
	mon_dump_caps_range(BOOT_PID, MONITOR, FS_PID, 0, S3K_CAP_CNT - 1);

	err = s3k_mon_resume(MONITOR, FS_PID);
	if (err) {
		alt_printf("s3k_mon_resume error code: %x\n", err);
		return -1;
	}

	wait_for_fs_ready();

	// Create PMP capability for region of memory where FS communication stuff will be held
	free_cidx = find_free_cidx();
	ASSERT(free_cidx
	       == S3K_CAP_CNT); // ASSERT IS OPPOSITE, if true you hang

	s3k_cap_t cap = s3k_mk_pmp(
	    s3k_napot_encode((uint64_t)&storage, sizeof(storage)), S3K_MEM_RW);
	ASSERT(s3k_cap_derive(self_mem_cidx, free_cidx, cap));
	s3k_cidx_t fs_pmp_cidx = free_cidx;
	free_cidx = find_free_cidx();

	s3k_reply_t reply = send_fs_client_init(client_cidx, fs_pmp_cidx);
	ASSERT(reply.err);
	ASSERT(reply.data[0]);

	wait_for_fs_ready();

	do_benchmark();

	alt_puts("Successful execution of test program");
}

static s3k_cidx_t find_free_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err == S3K_ERR_EMPTY) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

static s3k_cidx_t setup_fs_caps()
{
	s3k_cidx_t boot_tmp = S3K_CAP_CNT - 1;
	s3k_cidx_t next_fs_cidx = 0;
	s3k_pmp_slot_t next_fs_pmp = 0;
	s3k_err_t err = S3K_SUCCESS;

	// MEMORY+PMP
	{
		s3k_cap_t cap = s3k_mk_memory((uint64_t)FS_MEM,
					      (uint64_t)FS_MEM + FS_MEM_LEN,
					      S3K_MEM_RWX);
		s3k_cap_t ram_cap = {0};
		ASSERT(s3k_cap_read(RAM_MEM, &ram_cap));
		print_cap(ram_cap);
		alt_putchar('\n');
		print_cap(cap);
		alt_putchar('\n');

		ASSERT(s3k_cap_derive(
		    RAM_MEM, boot_tmp,
		    s3k_mk_memory((uint64_t)FS_MEM,
				  (uint64_t)FS_MEM + FS_MEM_LEN, S3K_MEM_RWX)));
		s3k_napot_t fs_mem_addr
		    = s3k_napot_encode((uint64_t)FS_MEM, FS_MEM_LEN);
		alt_printf("fs_mem_addr: 0x%x from 0x%x , 0x%x\n", fs_mem_addr,
			   FS_MEM, FS_MEM_LEN);
		uint64_t base, size;
		s3k_napot_decode(fs_mem_addr, &base, &size);
		alt_printf("base: 0x%x, size: 0x%x\n", base, size);
		ASSERT(s3k_cap_derive(boot_tmp, boot_tmp - 1,
				      s3k_mk_pmp(fs_mem_addr, S3K_MEM_RWX)));
		ASSERT(s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID,
					next_fs_cidx));
		next_fs_cidx++;
		ASSERT(s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp - 1, FS_PID,
					next_fs_cidx));
		ASSERT(s3k_mon_pmp_load(MONITOR, FS_PID, next_fs_cidx,
					next_fs_pmp));
		next_fs_cidx++;
		next_fs_pmp++;
	}

	// Derive a PMP capability for uart (to allow debug output)
	{
		s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
		ASSERT(s3k_cap_derive(UART_MEM, boot_tmp,
				      s3k_mk_pmp(uart_addr, S3K_MEM_RW)));
		ASSERT(s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID,
					next_fs_cidx));
		ASSERT(s3k_mon_pmp_load(MONITOR, FS_PID, next_fs_cidx,
					next_fs_pmp));
		next_fs_cidx++;
		next_fs_pmp++;
	}


	s3k_cidx_t client_sock_idx = 0;
	// SERVER SOCKET
	{
		ASSERT(s3k_cap_derive(CHANNEL, boot_tmp,
				      s3k_mk_socket(FS_CHANNEL, S3K_IPC_YIELD,
						    S3K_IPC_CCAP | S3K_IPC_CDATA
							| S3K_IPC_SCAP
							| S3K_IPC_SDATA,
						    0)));
		client_sock_idx = find_free_cidx();
		if (client_sock_idx == S3K_CAP_CNT) {
			ASSERT(S3K_ERR_INVALID_INDEX);
		}
		ASSERT(s3k_cap_derive(boot_tmp, client_sock_idx,
				      s3k_mk_socket(FS_CHANNEL, S3K_IPC_YIELD,
						    S3K_IPC_CCAP | S3K_IPC_CDATA
							| S3K_IPC_SCAP
							| S3K_IPC_SDATA,
						    1)));
		ASSERT(s3k_mon_cap_move(MONITOR, BOOT_PID, boot_tmp, FS_PID,
					next_fs_cidx));
		next_fs_cidx++;
	}

	// Write start PC of FS to PC
	ASSERT(
	    s3k_mon_reg_write(MONITOR, FS_PID, S3K_REG_PC, (uint64_t)FS_MEM));

	return client_sock_idx;
}
