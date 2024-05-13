#pragma once

/* Boot process */
#define BOOT_PID 0
#define BOOT_MEM ((char *)0x80010000ull)
#define BOOT_LEN (0x100000)

/* FS process */
#define FS_PID 1
#define FS_MEM ((char *)0x80200000ull)
#define FS_MEM_LEN (0x100000)
#define FS_CHANNEL 0
#define FS_DEBUG 0