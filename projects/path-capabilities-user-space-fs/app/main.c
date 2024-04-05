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
#define VIRTIO_MEM 10
#define ROOT_PATH 11

// Derived
#define UART_PMP 12
#define UART_PMP_SLOT 1
#define VIRTIO_PMP 13
#define VIRTIO_PMP_SLOT 2

int main(void)
{
	alt_puts("Hello from app, returning");
	return 0;
}
