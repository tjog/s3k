.POSIX:

export PLATFORM   ?=qemu_virt
export ROOT       :=${abspath ../..}
export BUILD      :=${abspath build/${PLATFORM}}
export S3K_CONF_H :=${abspath s3k_conf.h}

include ${ROOT}/common/plat/${PLATFORM}.mk

APPS=boot fs app

ELFS:=${patsubst %,${BUILD}/%.elf,kernel ${APPS}}

all: kernel ${APPS}

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

qemu: kernel ${APPS}
	@ELFS="${ELFS}" ${ROOT}/scripts/qemu.sh -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu-gdb: kernel ${APPS}
	@ELFS="${ELFS}" ${ROOT}/scripts/qemu.sh -gdb tcp::3333 -S

gdb: kernel ${APPS}
	@ELFS="${ELFS}" ${ROOT}/scripts/gdb.sh

gdb-openocd: kernel ${APPS}
	@ELFS="${ELFS}" ${ROOT}/scripts/gdb-openocd.sh

disk-image:
	qemu-img create fs.img 10M
	mformat -i fs.img ::
	echo "hello" > tmp.txt
	mcopy -i fs.img tmp.txt ::/test.txt
	rm tmp.txt

disk-read:
	mdir -i fs.img
	mcopy -i fs.img ::/test.txt extracted.txt
	cat < extracted.txt
	rm extracted.txt

${APPS}: common
	@${MAKE} -f build.mk PROGRAM=$@

.PHONY: all clean qemu qemu-gdb gdb kernel common ${APPS}
