#pragma once

/* Boot process */
#define BOOT_PID 0
#define BOOT_MEM ((char *)0x80010000ull)
#define BOOT_LEN (0x10000)

/* FS process */
#define FS_PID 1
#define FS_MEM ((char *)0x80020000ull)
#define FS_MEM_LEN (0x10000)
#define FS_CHANNEL 0
#define FS_DEBUG 1

/* Sign process */
#define SIGN_PID 2
#define SIGN_MEM ((char *)0x80030000ull)
#define SIGN_MEM_LEN (0x10000)
#define SIGN_CHANNEL 1
#define SIGN_DEBUG 1

/* Application memory area */
#define APP_PID 3
#define APP_MEM ((char *)0x80040000ull)
#define APP_MEM_LEN (0x10000)