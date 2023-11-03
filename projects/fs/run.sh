#!/bin/bash

KERNEL=build/s3k
APP0=build/app0

# Build the kernel and application
make all

# Run the kernel, load the application as payload
qemu-system-riscv64 -M virt -smp 1 -m 128M			\
	-nographic -bios none -kernel $KERNEL.elf		\
	-device loader,file=$APP0.bin,addr=0x80010000	\
	-drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
