#include "altc/altio.h"
#include "altc/string.h"
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

#define KibiBytes(X) ((1 << 10) * (X))
#define MibiBytes(X) ((1 << 20) * (X))

#define PROCESS_NAME "app"

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


s3k_cidx_t find_mem_slice_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_MEMORY) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

s3k_cidx_t find_path_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err)
			continue;
		if (c.type == S3K_CAPTY_PATH) {
			return i;
		}
	}
	return S3K_CAP_CNT;
}

s3k_cidx_t find_free_cidx()
{
	for (s3k_cidx_t i = 0; i < S3K_CAP_CNT; i++) {
		s3k_cap_t c;
		s3k_err_t err = s3k_cap_read(i, &c);
		if (err == S3K_ERR_EMPTY)
			return i;
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
	s3k_sync_mem();
	alt_puts(PROCESS_NAME ": Hello from boot app ");

	s3k_cidx_t path_idx = find_path_cidx();
	if (path_idx == S3K_CAP_CNT) {
		alt_printf(PROCESS_NAME ": error: could not find path capability\n");
		return -1;
	}
	s3k_cidx_t tmp_idx = find_free_cidx();
	err = s3k_path_derive(path_idx, "app", tmp_idx, KibiBytes(10), PATH_READ | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	path_idx = tmp_idx;
	tmp_idx = find_free_cidx();

	// Create dir
	alt_puts("Create dir:");
	err = s3k_path_derive(path_idx, NULL, tmp_idx, KibiBytes(5), PATH_READ | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	err = s3k_create_dir(tmp_idx, false);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_create_dir returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	alt_puts("");

	// Write file
	alt_puts("Write file:");
	char b[] = "Hello, this is something to test writing with.";
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, sizeof(b), FILE | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	uint32_t bw = 0;
	err = s3k_write_file(tmp_idx, 0, b, sizeof(b),
			     &bw); // Happen to know metadata size is 32 bytes
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_write_file returned error %d\n", err);
		return -1;
	}
	alt_printf(PROCESS_NAME ": wrote %d bytes:\n", bw);
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	char partial_overlap[] = "overlap is four";
	bw = 0;
	err = s3k_write_file(tmp_idx, sizeof(b) - 4, partial_overlap, sizeof(partial_overlap), &bw);
	if (err == S3K_ERR_FILE_SIZE) {
		alt_puts("Partial overlap failed as expected.");
	} else if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_write_file returned unexpected error %d\n",
			   err);
		return -1;
	}
	err = s3k_write_file(tmp_idx, sizeof(b) - sizeof(partial_overlap), partial_overlap,
			     sizeof(partial_overlap), &bw);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_write_file returned error %d\n", err);
		return -1;
	}
	alt_puts("Full overlap succeeded as expected.");
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	alt_puts("");

	// Read file
	alt_puts("Read file:");
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, 10, FILE | PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	uint8_t buf[100];
	uint32_t bytes_read = 0;
	err = s3k_read_file(tmp_idx, 0, buf, sizeof(buf) - 1, &bytes_read);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_read_file returned error %d\n", err);
		return -1;
	}
	alt_printf(PROCESS_NAME ": read %d bytes:\n", bytes_read);

	if (buf[bytes_read] != 0)
		buf[bytes_read] = 0;
	alt_printf(PROCESS_NAME ": read %d bytes:\n%s\n", bytes_read, buf);
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	alt_puts("");

	// Read dir
	alt_puts("Read dir:");
	err = s3k_path_derive(path_idx, NULL, tmp_idx, 0, PATH_READ);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	s3k_dir_entry_info_t dei;
	err = s3k_read_dir(tmp_idx, 0, &dei);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_read_dir returned error %d\n", err);
		return -1;
	}
	alt_puts(PROCESS_NAME ": directory read");
	alt_printf("Entry: fattrib=0x%X, fdate=0x%X, ftime=0x%X, fsize=%d fname=%s\n", dei.fattrib,
		   dei.fdate, dei.ftime, dei.fsize, dei.fname);
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	alt_puts("");

	// Delete file
	alt_puts("Delete file:");
	err = s3k_path_derive(path_idx, "test.txt", tmp_idx, 10, FILE | PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	err = s3k_path_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_path_delete returned error %d\n", err);
		return -1;
	}
	alt_puts(PROCESS_NAME ": file deleted");
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	alt_puts("");

	// Delete directory
	alt_puts("Delete dir:");
	err = s3k_path_derive(path_idx, NULL, tmp_idx, 0, PATH_WRITE);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_derive returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);
	err = s3k_path_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_path_delete returned error %d\n", err);
		return -1;
	}
	alt_puts(PROCESS_NAME ": directory deleted");
	err = s3k_cap_delete(tmp_idx);
	if (err) {
		alt_printf(PROCESS_NAME ": error: s3k_cap_delete returned error %d\n", err);
		return -1;
	}
	dump_caps_range(PROCESS_NAME, path_idx, tmp_idx);

	alt_puts("Test program finished");
}