# Executed-instruction cycle measurement under QEMU + GDB.
# Requires: arm-none-eabi-gcc, qemu-system-arm, gdb-multiarch.
CC     := arm-none-eabi-gcc
CFLAGS := -O2 -mcpu=cortex-m3 -mthumb -nostdlib -ffreestanding
PY     ?= python3
PORT   ?= 1234

.PHONY: all run clean
all: bench_m3.elf

bench_m3.elf: bench.c startup.c tt_tag.c hmac_sha1.c link.ld
	$(CC) $(CFLAGS) -T link.ld startup.c bench.c hmac_sha1.c tt_tag.c -o $@

run: bench_m3.elf ## trace and print exact executed cycles
	@qemu-system-arm -M lm3s6965evb -cpu cortex-m3 -kernel bench_m3.elf \
	   -nographic -S -gdb tcp::$(PORT) & \
	 sleep 2; \
	 gdb-multiarch -q -batch -x trace.py bench_m3.elf | grep -E "RESULT|mix"; \
	 pkill -f qemu-system-arm || true

clean:
	rm -f *.elf *.o
