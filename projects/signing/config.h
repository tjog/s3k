#pragma once

/* Boot process */
#define BOOT_PID 0
#define BOOT_MEM ((char *)0x80010000ull)
#define BOOT_LEN (0x10000)

/* Sign process */
#define SIGN_PID 1
#define SIGN_MEM ((char *)0x80020000ull)
#define SIGN_MEM_LEN (0x10000)
#define SIGN_CHANNEL 0

/* Application memory area */
#define APP_PID 2
#define APP_MEM ((char *)0x80030000ull)
#define APP_MEM_LEN (0x10000)