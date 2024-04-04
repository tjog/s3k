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
#define app0_PATH 13
#define main_c_PATH 14
#define test_bin_PATH 15
#define temp_PATH 16

#define KibiBytes(X) ((1 << 10) * (X))
#define MibiBytes(X) ((1 << 20) * (X))

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


int main(void)
{
	s3k_napot_t uart_addr = s3k_napot_encode(UART0_BASE_ADDR, 0x8);
	s3k_err_t err
	    = setup_pmp_from_mem_cap(UART_MEM, UART_PMP, UART_PMP_SLOT, uart_addr, S3K_MEM_RW);
	if (err)
		alt_printf("Uart setup error code: %x\n", err);
	alt_puts("finished setting up uart");

	dump_caps_range("Capability", 0, 16);

	err = s3k_path_derive(ROOT_PATH, "app0", app0_PATH, KibiBytes(10), PATH_READ | PATH_WRITE);
	if (err) {
		alt_printf("Error from path derive '%s': 0x%X\n", "app0", err);
		return -1;
	}
	err = s3k_path_derive(app0_PATH, "main.c", main_c_PATH, KibiBytes(2), FILE | PATH_READ);
	if (err) {
		alt_printf("Error from path derive '%s': 0x%X\n", "main.c", err);
		return -1;
	}
	err = s3k_path_derive(app0_PATH, "test.bin", test_bin_PATH, KibiBytes(2),
			      FILE | PATH_WRITE);
	if (err) {
		alt_printf("Error from path derive '%s': 0x%X\n", "test.bin", err);
		return -1;
	}
	char b[] = "Hello";
	uint32_t res = 0;
	err = s3k_write_file(test_bin_PATH, KibiBytes(1), b, sizeof(b), &res);
	// err = s3k_write_file(test_bin_PATH, KibiBytes(4) - sizeof(b), b, sizeof(b), &res);
	if (err) {
		alt_printf("Error from s3k_write_file: %d\n", err);
		return -1;
	}
	alt_puts("Successful write");
	dump_caps_range("Capability", 0, 20);

	/**
	 * TEST CREATING DIRECTORY entries overflowing max size
	*/
	b[1] = 0;
	for (char i = 'A'; i <= 'Z'; i++) {
		b[0] = i;
		err = s3k_path_derive(app0_PATH, b, temp_PATH, KibiBytes(1), PATH_WRITE);
		if (err) {
			alt_printf("Error from path derive '%s': 0x%X\n", b, err);
			return -1;
		}
#if 1
		err = s3k_create_dir(temp_PATH, false);
		if (err) {
			alt_printf("Error from s3k_create_dir '%s': 0x%X\n", b, err);
			return -1;
		}
		alt_printf("Succesful dir creation of %s\n", b);
#else
		s3k_path_delete(temp_PATH);
		alt_printf("Succesful dir deletion of %s\n", b);
#endif
		s3k_cap_delete(temp_PATH);
	}

	// uint8_t buf[50];
	// uint32_t bytes_read;
	// err = s3k_read_file(newfile_PATH, 0, buf, sizeof(buf) - 1, &bytes_read);
	// if (err) {
	// 	alt_printf("Error from s3k_read_file: %d\n", err);
	// 	return -1;
	// }
	// buf[bytes_read] = 0;
	// alt_printf("Successful read, contents:\n%s\n", buf);

	// err = s3k_create_dir(newdir_PATH,
	// 		     false); // could add "ensure create" flag here
	// if (err) {
	// 	alt_printf("Error from s3k_create_dir: %d\n", err);
	// 	return -1;
	// }
	// char b[] = "Hello";
	// uint32_t res = 0;
	// err = s3k_write_file(nested_PATH, 0, b, sizeof(b), &res);
	// if (err) {
	// 	alt_printf("Error from s3k_write_file: %d\n", err);
	// 	return -1;
	// }
	// alt_puts("Successful write");
	// err = s3k_read_file(nested_PATH, 0, buf, sizeof(buf) - 1, &bytes_read);
	// if (err) {
	// 	alt_printf("Error from s3k_read_file: %d\n", err);
	// 	return -1;
	// }
	// buf[bytes_read] = 0;
	// alt_printf("Successful read, contents:\n%s\n", buf);
	// err = s3k_path_delete(nested_PATH);
	// if (err) {
	// 	alt_printf("Error from s3k_path_delete: %d\n", err);
	// 	return -1;
	// }
	// alt_puts("Successful delete of nested");
	// err = s3k_read_file(nested_PATH, 0, buf, sizeof(buf) - 1, &bytes_read);
	// if (err) {
	// 	alt_printf("Expected error from s3k_read_file: %d == %d = %d\n", err,
	// 		   S3K_ERR_FILE_OPEN, err == S3K_ERR_FILE_OPEN);
	// }
	// buf[bytes_read] = 0;
	// err = s3k_path_delete(newdir_PATH);
	// if (err) {
	// 	alt_printf("Error from s3k_path_delete: %d\n", err);
	// 	return -1;
	// }
	// alt_puts("Successful delete of newdir");

	s3k_dir_entry_info_t dei;
	for (size_t i = 0; i < 5; i++) {
		err = s3k_read_dir(ROOT_PATH, i, &dei);
		if (err) {
			alt_printf("Error from s3k_read_dir: %d\n", err);
		} else {
			alt_printf(
			    "Entry: fattrib=0x%X, fdate=0x%X, ftime=0x%X, fsize=%d fname=%s\n",
			    dei.fattrib, dei.fdate, dei.ftime, dei.fsize, dei.fname);
		}
	}
	alt_puts("Successful execution of test program");
}