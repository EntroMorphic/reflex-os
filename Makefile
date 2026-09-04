# Reflex OS — Build & Release Targets
# Requires: ESP-IDF environment (source export.sh)

VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
RELEASE_NAME := reflex-os-$(VERSION)-esp32c6
RELEASE_DIR := release/$(RELEASE_NAME)

.PHONY: build flash release clean test tasm-test hw-test doc-links format format-check \
        format-diff warn-check lock-check ci-lint soc-header soc-bridge soc-check \
        idf-build verify config-reset docs atlas

build:
	idf.py build

flash:
	idf.py flash

release: build
	@echo "Packaging $(RELEASE_NAME)..."
	@rm -rf $(RELEASE_DIR) $(RELEASE_DIR).zip
	@mkdir -p $(RELEASE_DIR)
	@cp build/bootloader/bootloader.bin $(RELEASE_DIR)/
	@cp build/partition_table/partition-table.bin $(RELEASE_DIR)/
	@cp build/reflex_os.bin $(RELEASE_DIR)/
	@cp release/flash.sh $(RELEASE_DIR)/
	@echo "Reflex OS $(VERSION)" > $(RELEASE_DIR)/VERSION
	@echo "" >> $(RELEASE_DIR)/VERSION
	@echo "Flash:" >> $(RELEASE_DIR)/VERSION
	@echo "  pip install esptool" >> $(RELEASE_DIR)/VERSION
	@echo "  ./flash.sh" >> $(RELEASE_DIR)/VERSION
	@echo "" >> $(RELEASE_DIR)/VERSION
	@echo "Shell:" >> $(RELEASE_DIR)/VERSION
	@echo "  screen /dev/cu.usbmodem1101 115200" >> $(RELEASE_DIR)/VERSION
	@cd release && zip -r $(RELEASE_NAME).zip $(RELEASE_NAME)/
	@echo ""
	@echo "Release: release/$(RELEASE_NAME).zip"
	@ls -lh release/$(RELEASE_NAME).zip

test:
	$(MAKE) -C tests/host test

tasm-test:
	python3 tests/host/test_tasm.py

# Shell validation against a flashed board. Non-destructive: never provisions
# or clears an Aura key, never reboots, restores role/vitals/purpose.
#   make hw-test PORT=/dev/cu.usbmodem1101
doc-links:
	python3 tools/check_doc_links.py

# Compile the ESP-independent firmware for real, at -O2, with -Wall -Wextra
# -Werror. A syntax check is not a build: `gcc -fsyntax-only` generates no
# code and so runs none of the flow-sensitive analyses.
warn-check:
	@tools/check_warnings.sh

# No telemetry emission may happen while the loom lock is held.
lock-check:
	@python3 tools/check_lock_discipline.py

# Regenerate Reflex's own SoC register header from the vendor SVD.
soc-header:
	python3 tools/soc_scraper.py --emit-bridge

# Prove every generated SoC constant equals the ESP-IDF macro it replaced, by
# compiling a translation unit of _Static_asserts with the real toolchain.
# Requires a prior `make idf-build` for compile_commands.json.
soc-bridge:
	@tools/check_soc_bridge.sh

# Fail if the generated header is stale relative to the SVD or the mapping.
soc-check:
	@python3 tools/soc_scraper.py --check

# Validate the workflow file itself. Renaming a job is not a local edit: the
# `release` job's `needs:` list referred to a job that had been renamed, which
# GitHub rejects at parse time — no jobs run at all, and the failure reports as
# "a workflow file issue" rather than naming the dangling reference.
# Schema only: shellcheck flags pre-existing SC2086 style in jobs this does not
# own, and failing on that would make the gate noise rather than signal.
ci-lint:
	@command -v docker >/dev/null 2>&1 || { echo "docker required for ci-lint"; exit 1; }
	@docker run --rm -v "$$PWD":/repo -w /repo $(ACTIONLINT_IMAGE) -no-color -shellcheck= \
	  && echo "Workflows: schema valid."
ACTIONLINT_IMAGE ?= rhysd/actionlint:latest

# The real ESP-IDF toolchain, in the image CI uses, without installing it.
# This is the only local check that speaks for the firmware build; everything
# above is an approximation of it.
IDF_IMAGE ?= espressif/idf:release-v5.5
IDF_TARGET_ ?= esp32c6
idf-build:
	@command -v docker >/dev/null 2>&1 || { echo "docker required for idf-build"; exit 1; }
	docker run --rm -u "$$(id -u):$$(id -g)" -e HOME=/tmp -e IDF_COMPONENT_MANAGER=0 \
	    -v "$$PWD":/work -w /work $(IDF_IMAGE) \
	    bash -c '. $$IDF_PATH/export.sh >/dev/null 2>&1 && \
	             idf.py -B build set-target $(IDF_TARGET_) >/dev/null && \
	             idf.py -B build build'

# Everything runnable without a board. Run this before pushing firmware changes.
verify: test tasm-test doc-links warn-check lock-check ci-lint soc-check idf-build soc-bridge
	@echo ""
	@echo "verify: host tests, TASM, doc links, warning gate, lock discipline, workflow schema, a real ESP-IDF build, and the SoC constant bridge all passed."

hw-test:
	@test -n "$(PORT)" || { echo "Usage: make hw-test PORT=/dev/cu.usbmodemXXXX"; exit 1; }
	python3 tests/hardware/validate_shell.py $(PORT)

atlas:
	python3 tools/goose_scraper.py tools/esp32c6.svd tools/goose_zones.json components/goose/goose_shadow_atlas.c
	@echo "Shadow atlas regenerated. Run 'idf.py build' to verify."

format:
	find . -name '*.c' -o -name '*.h' | grep -v build | grep -v esp-idf | xargs clang-format -i

# Whole-tree formatting report. Advisory: the tree does not currently satisfy
# .clang-format and bringing it into line is a deliberate reformat, not a
# side effect of a check. Reports honestly and exits non-zero — the previous
# version piped clang-format into `head`, which discarded its exit status, and
# then printed "Format check passed." unconditionally, so it announced success
# while listing violations and could never fail. CI gates on format-diff.
format-check:
	@find . -name '*.c' -o -name '*.h' | grep -v build | grep -v esp-idf \
	    | xargs clang-format --dry-run --Werror

# What CI gates on: the lines this change touches must be formatted, without
# demanding a tree-wide reformat that would bury history in `git blame`.
# BASE defaults to the previous commit; CI passes the merge base for a PR.
format-diff:
	@command -v git-clang-format >/dev/null 2>&1 || { \
	    echo "git-clang-format not found (pip install clang-format)"; exit 1; }
	@out=$$(git-clang-format --diff --extensions c,h $(or $(BASE),HEAD~1) -- 2>&1); \
	 case "$$out" in \
	   *"did not modify any files"*|"") echo "Format: changed lines are clean." ;; \
	   *"no modified files"*)           echo "Format: no C/H changes to check." ;; \
	   *) echo "$$out"; echo; \
	      echo "Changed lines are not formatted. Fix with: git-clang-format $(or $(BASE),HEAD~1)"; \
	      exit 1 ;; \
	 esac

docs:
	@command -v doxygen >/dev/null 2>&1 || { echo "Error: doxygen not installed. Install with: brew install doxygen"; exit 1; }
	doxygen Doxyfile
	@echo "API docs: docs/api/html/index.html"

config-reset:
	rm -f sdkconfig
	idf.py set-target esp32c6
	@echo "sdkconfig regenerated from defaults"

clean:
	idf.py fullclean
