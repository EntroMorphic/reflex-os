# Reflex OS — Build & Release Targets
# Requires: ESP-IDF environment (source export.sh)

VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
RELEASE_NAME := reflex-os-$(VERSION)-esp32c6
RELEASE_DIR := release/$(RELEASE_NAME)

.PHONY: build flash release clean test format config-reset

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

format:
	find . -name '*.c' -o -name '*.h' | grep -v build | grep -v esp-idf | xargs clang-format -i

config-reset:
	rm -f sdkconfig
	idf.py set-target esp32c6
	@echo "sdkconfig regenerated from defaults"

clean:
	idf.py fullclean
