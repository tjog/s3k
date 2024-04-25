.POSIX:

export PLATFORM   ?=qemu_virt
export ROOT       :=${abspath ../..}
export BUILD      :=${abspath build/${PLATFORM}}
export S3K_CONF_H :=${abspath s3k_conf.h}

include ${ROOT}/tools.mk
include ${ROOT}/common/plat/${PLATFORM}.mk

ELFS=${patsubst %, ${BUILD}/%.elf, kernel}

all: kernel size

clean:
	@${MAKE} -C ${ROOT}/common clean
	@${MAKE} -C ${ROOT}/kernel clean
	@for prog in ${APPS}; do \
		${MAKE} -f build.mk PROGRAM=$$prog clean; \
		done

common:
	@${MAKE} -C ${ROOT}/common

kernel: common
	@${MAKE} -C ${ROOT}/kernel

size: kernel
	${SIZE} ${BUILD}/*.elf

.PHONY: all clean common kernel size
