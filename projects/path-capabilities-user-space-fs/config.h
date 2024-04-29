#pragma once

/* Boot process */
#define BOOT_PID 0
#define BOOT_MEM ((char *)0x80010000ull)
#define BOOT_LEN (0x10000)

/* FS process */
#define FS_PID 1
#define FS_MEM ((char *)0x80020000ull)
#define FS_MEM_LEN (0x110000)
#define FS_CHANNEL 0

/* Application memory area */
#define APP_PID 2
#define APP_MEM ((char *)0x80130000ull)
#define APP_MEM_LEN (0x10000)
