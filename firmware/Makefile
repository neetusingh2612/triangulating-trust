# ============================================================================
# Bench firmware. BOARD=s32k144 (default, automotive-grade) | stm32f103
#
#   make                       build all roles for $(BOARD)
#   make BOARD=stm32f103       build for the STM32 board instead
#   make ALPHA09=1             build with alpha=0.9 (reproduces the reported
#                              605-cycle measurement; default is alpha=7/8)
#   make selftest              build ROLE_CYCLES with the CAN loopback check
#
# Flash ROLE_CYCLES on one board for per-component cycle counts (no bus
# needed). Flash TX + TX_baseline on the sender and RX on the receiver for
# bus-utilisation, drop counts, and per-frame cost under live load.
# ============================================================================
BOARD   ?= s32k144
CC      := arm-none-eabi-gcc
PY      ?= python3

ifeq ($(BOARD),s32k144)
  MCPU   := cortex-m4
  CLOCK  := 80000000
  BDIR   := boards/s32k144
  SRCS   := $(BDIR)/can_hal_s32k144.c $(BDIR)/startup_s32k144.c
  LD     := $(BDIR)/link_s32k144.ld
else ifeq ($(BOARD),stm32f103)
  MCPU   := cortex-m3
  CLOCK  := 72000000
  BDIR   := boards/stm32f103
  SRCS   := $(BDIR)/can_hal_stm32f103.c $(BDIR)/startup_stm32f103.c
  LD     := $(BDIR)/link_stm32f103.ld
else
  $(error BOARD must be s32k144 or stm32f103)
endif

CFLAGS  := -mcpu=$(MCPU) -mthumb -mfloat-abi=soft -O2 \
           -ffunction-sections -fdata-sections -Wall -Icommon
SPECS   := --specs=nano.specs --specs=nosys.specs
LDFLAGS := -T $(LD) -Wl,--gc-sections -nostartfiles $(SPECS)
COMMON  := common/dut.c $(SRCS)
DEFS    := -DCORE_HZ=$(CLOCK)
ifdef ALPHA09
  DEFS  += -DTT_ALPHA_09
endif

ROLES   := ROLE_CYCLES ROLE_TX ROLE_RX

.PHONY: all selftest clean
all:
	@for r in $(ROLES); do \
	  $(CC) $(CFLAGS) $(DEFS) -D$$r common/main.c $(COMMON) $(LDFLAGS) \
	     -o $(BOARD)_$$r.elf || exit 1; \
	done
	@$(CC) $(CFLAGS) $(DEFS) -DROLE_TX -DTAG_ENABLED=0 common/main.c $(COMMON) \
	   $(LDFLAGS) -o $(BOARD)_TX_baseline.elf
	@echo ""
	@arm-none-eabi-size $(BOARD)_*.elf
	@echo ""
	@echo "Verify the timed code is resident in RAM (.ramfunc):"
	@arm-none-eabi-nm $(BOARD)_ROLE_CYCLES.elf | grep -E " dut_(tag|voronoi|ema)" || true

selftest: ## build ROLE_CYCLES with the CAN loopback self-test enabled
	$(CC) $(CFLAGS) $(DEFS) -DROLE_CYCLES -DRUN_SELFTEST common/main.c $(COMMON) \
	   $(LDFLAGS) -o $(BOARD)_SELFTEST.elf
	@echo "Flash $(BOARD)_SELFTEST.elf. It must print PASS before any"
	@echo "measurement is trustworthy."

clean:
	rm -f *.elf
