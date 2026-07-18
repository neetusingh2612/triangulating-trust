# Symbolic verification. Requires ProVerif >= 2.05 (https://bblanche.gitlabpages.inria.fr/proverif/)
PROVERIF ?= proverif

.PHONY: all clean
all:
	@echo "=== Model A: honest parties ==="
	$(PROVERIF) tt_modelA_honest.pv  | tail -20
	@echo ""
	@echo "=== Model B: ECU capture (group-key attribution gap expected) ==="
	$(PROVERIF) tt_modelB_capture.pv | tail -20

clean:
	@true
