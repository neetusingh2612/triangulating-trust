# ============================================================================
# Triangulating Trust -- top-level Makefile
#
#   make help          list targets
#   make install       install the Python package (editable) + ML extras
#   make verify        fast self-checks that need no dataset
#   make analysis      full analyses that need the ROAD dataset (see DATA)
#   make cycles        static ARM cycle counts (needs arm-none-eabi-gcc)
#   make trace         executed-instruction cycle measurement (needs QEMU+GDB)
#   make firmware      build bench firmware for BOARD=s32k144|stm32f103
#   make proverif      symbolic verification (needs proverif)
#   make clean
#
# Dataset: the ROAD corpus is NOT redistributed here. Download it and point
# DATA at the directory holding the .log files:
#   make analysis DATA=/path/to/road
# ============================================================================

PY      ?= python3
DATA    ?= ./data
BOARD   ?= s32k144
AMBIENT ?= $(DATA)/ambient_dyno_drive_basic_short.log
FUZZING ?= $(DATA)/fuzzing_attack_1.log
META    ?= $(DATA)/capture_metadata.json

.PHONY: help install verify analysis cycles trace firmware proverif clean test

help:
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
	  | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-12s\033[0m %s\n",$$1,$$2}'

install: ## install package (editable) with ML extras
	$(PY) -m pip install -e ".[ml]"

verify: ## self-checks requiring no dataset (tag vectors, FRR, avalanche smoke)
	@echo "--- tag reference self-test ---"
	$(PY) -c "from triangulating_trust.tt_tag import tag, catalan_key_from_seed; \
	          R=0x0F1E2D3C4B5A69788796A5B4C3D2E1F0; k=catalan_key_from_seed(R,30); \
	          print('schedule len', len(k)); \
	          print('tag', hex(tag(R,0x123,bytes(8),5,k,t=16)))"
	@echo "--- fuzzy-extractor FRR (Table: frr) ---"
	$(PY) -m triangulating_trust.frr_sim

analysis: ## full evaluation on the ROAD dataset (set DATA=...)
	@test -f "$(AMBIENT)" || { echo "ERROR: $(AMBIENT) not found. Set DATA=/path/to/road"; exit 1; }
	$(PY) -m triangulating_trust.beta_containment --trace "$(AMBIENT)" --out results/beta_road.json
	$(PY) -m triangulating_trust.detection_eval --ambient "$(AMBIENT)" --fuzzing "$(FUZZING)"
	$(PY) -m triangulating_trust.mac_detection --ambient "$(AMBIENT)" \
	      --masq_dir "$(DATA)" --meta "$(META)"

cycles: ## static ARM cycle counts (M0+/M3/M4)
	$(MAKE) -C native/arm

trace: ## executed-instruction cycle measurement under QEMU
	$(MAKE) -C native/qemu

firmware: ## build bench firmware (BOARD=s32k144 or stm32f103)
	$(MAKE) -C firmware BOARD=$(BOARD)

proverif: ## symbolic verification of both models
	$(MAKE) -C formal

test: ## run the test suite
	$(PY) -m pytest -q tests/

clean:
	$(MAKE) -C native/arm clean  || true
	$(MAKE) -C native/qemu clean || true
	$(MAKE) -C firmware clean    || true
	find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	rm -rf build dist *.egg-info src/*.egg-info
