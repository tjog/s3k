This project contains a simple FAT32 filesystem that stores data in a virtio
disk

- The implementation of the filesystem is http://elm-chan.org/fsw/ff/
- The VirtIO support is taken from
  https://github.com/mit-pdos/xv6-riscv/tree/riscv
  
To run the program, you first have to create a disk image using ``make
disk-image``.
To check the result of accessing the filesystem, you can use ``make disk-read``.
