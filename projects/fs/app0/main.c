#include "altc/altio.h"
#include "s3k/s3k.h"

#include "ff.h"


int main(void)
{
	uint64_t uart_addr = s3k_napot_encode(0x10000000, 0x2000);
	while (s3k_cap_derive(2, 16, s3k_mk_pmp(uart_addr, S3K_MEM_RW)))
		;
	while (s3k_pmp_load(16, 1))
		;
	s3k_sync();
	alt_puts("hello, world");

	FATFS FatFs;		/* FatFs work area needed for each volume */
	f_mount(&FatFs, "", 0);		/* Give a work area to the default drive */
	alt_puts("File system mounted");

	UINT bw;
	FRESULT fr;
	FIL Fil;			/* File object needed for each open file */
	fr = f_open(&Fil, "newfile.txt", FA_WRITE | FA_CREATE_ALWAYS);	/* Create a file */
	if (fr == FR_OK) {
		f_write(&Fil, "It works!\r\n", 11, &bw);	/* Write data to the file */
		fr = f_close(&Fil);							/* Close the file */
		if (fr == FR_OK && bw == 11) {
			alt_puts("File saved\n");
		}
	} else{
		alt_puts("File not opened\n");
	}

	char buffer[1024];
	fr = f_open(&Fil, "test.txt", FA_READ);
	if (fr == FR_OK) {
		f_read(&Fil, buffer, 1023, &bw);	/*Read data from the file */
		fr = f_close(&Fil);							/* Close the file */
		if (fr == FR_OK) {
			buffer[bw] = '\0';
			alt_puts(buffer);
		}
	} else{
		alt_puts("File not opened\n");
	}
}
