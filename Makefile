# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

vpath %.c src
vpath %.S src

ASSRC=head.S trap.S stack.S
CSRC=cap.c cnode.c csr.c exception.c init.c lock.c proc.c schedule.c syscall.c\
     syscall_monitor.c timer.c wfi.c
OBJ=${addprefix obj/, ${ASSRC:.S=.o} ${CSRC:.c=.o}}
DEP=${OBJ:.o=.d}

CONFIG_H?=config.h
PLATFORM_H?=plat/${PLATFORM}/platform.h

all: options s3k.elf s3k.da

docs:
	doxygen

options:
	@printf "build options:\n"
	@printf "CC       = ${CC}\n"
	@printf "LDFLAGS  = ${LDFLAGS}\n"
	@printf "ASFLAGS  = ${ASFLAGS}\n"
	@printf "CFLAGS   = ${CFLAGS}\n"
	@printf "CONFIG_H = ${abspath ${CONFIG_H}}\n"


obj/%.o: %.S
	@mkdir -p ${@D}
	${CC} ${ASFLAGS} -include ${CONFIG_H} -include ${PLATFORM_H} ${INC} -MMD -c -o $@ $<

obj/%.o: %.c
	@mkdir -p ${@D}
	${CC} ${CFLAGS} -include ${CONFIG_H} -include ${PLATFORM_H} ${INC}  -MMD -c -o $@ $<

s3k.elf: ${OBJ}
	${CC} ${LDFLAGS} -o $@ ${OBJ}

s3k.da:	s3k.elf
	${OBJDUMP} -d $< > $@

format:
	clang-format -i $(wildcard **/*.h) $(wildcard **/*.c) $(wildcard **/*.cc)

clean:
	git clean -fdX

-include ${DEP}

.PHONY: all options clean docs
