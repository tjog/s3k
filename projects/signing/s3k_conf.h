#pragma once

#define PLATFORM_VIRT
#include "plat/config.h"

// Number of user processes
#define S3K_PROC_CNT 3

// Number of capabilities per process.
#define S3K_CAP_CNT 32

// Number of IPC channels.
#define S3K_CHAN_CNT 2

// Maximum length of a PATH, impacts static storage requirement of Path capabilities
// (in multiplicative combination with S3K_MAX_PATH_CAPS)
#define S3K_MAX_PATH_LEN 100

// Maximum number of PATH capabilities total
#define S3K_MAX_PATH_CAPS 100

// Number of slots per period
#define S3K_SLOT_CNT 32ull

// Length of slots in ticks.
#define S3K_SLOT_LEN (S3K_RTC_HZ / S3K_SLOT_CNT)

// Scheduler time
#define S3K_SCHED_TIME (S3K_SLOT_LEN)

// If debugging, comment
#define NDEBUG
#define INSTRUMENT
