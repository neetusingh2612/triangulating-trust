# Static ARM cycle counting. Requires arm-none-eabi-gcc.
CC      := arm-none-eabi-gcc
CFLAGS  := -O2 -mthumb -ffunction-sections -fdata-sections -Wall
CORES   := cortex-m0plus cortex-m3 cortex-m4
PY      ?= python3

.PHONY: all clean
all:
	@for cpu in $(CORES); do \
	  $(CC) $(CFLAGS) -mcpu=$$cpu -c tt_tag.c        -o tt_$$cpu.o; \
	  $(CC) $(CFLAGS) -funroll-loops -mcpu=$$cpu -c tt_voronoi.c     -o v_$$cpu.o; \
	  $(CC) $(CFLAGS) -funroll-loops -mcpu=$$cpu -c tt_voronoi_a78.c -o a78_$$cpu.o; \
	  $(CC) $(CFLAGS) -mcpu=$$cpu -c hmac_sha1.c     -o hmac_$$cpu.o; \
	done
	@echo ""
	@echo "=== per-component cycle counts (paper Table: lowend) ==="
	@for cpu in $(CORES); do \
	  echo "--- $$cpu ---"; \
	  $(PY) cyclecount.py tt_$$cpu.o  tt_tag_unrolled  $$cpu | sed -n '2,5p'; \
	  $(PY) cyclecount.py v_$$cpu.o   tt_voronoi_check $$cpu | sed -n '5p'; \
	  $(PY) cyclecount.py a78_$$cpu.o tt_ema_update    $$cpu | sed -n '5p'; \
	done

clean:
	rm -f *.o
