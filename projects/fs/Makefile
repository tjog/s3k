export BUILD=${abspath build}
export S3K_CONF_H=${abspath s3k_conf.h}

DIRS=../../kernel app0

all: ${DIRS}

${DIRS}:
	@${MAKE} -C $@

clean:
	rm -rf ${BUILD}

disk-image:
	qemu-img create fs.img 10M
	mformat -i fs.img ::
	echo "hello" > tmp.txt
	mcopy -i fs.img tmp.txt ::/test.txt
	rm tmp.txt

disk-read:
	mdir -i fs.img
	mcopy -i fs.img ::/NEWFILE.TXT extracted.txt
	cat < extracted.txt
	rm extracted.txt

.PHONY: all clean ${DIRS}
