This project implements a use case where a path capability is sent to a signing process, as it is the only one with access to a private key.

To run the project, use the `gen_weak_rsa_key.py` script to generate two files containing a public and private key pair. The number of bits is
intentionally very low to enable running on a fixed width integer.

If you change the bit width, also change the `RSA_BITS_TYPE` in the C code.

Use the make steps `make disk-image disk-make-dirs disk-copy-keys` to create `fs.img` which is mounted during `make qemu` which runs the program.

The program performs a short boot setup before the SIGN and APP processes do yielding IPC to one another to both try signing a document (client gets back
a encrypted SHA256 digest) and the client using a public key to validate the signature by calculating the digest by its own and decrypting the
received digest bits using a public key.

## Before

This project contains a simple FAT32 filesystem that stores data in a virtio
disk

- The implementation of the filesystem is http://elm-chan.org/fsw/ff/
- The VirtIO support is taken from
  https://github.com/mit-pdos/xv6-riscv/tree/riscv
  
To run the program, you first have to create a disk image using ``make
disk-image``.
To check the result of accessing the filesystem, you can use ``make disk-read``.
