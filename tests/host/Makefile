# Reflex OS Host Test Runner
#
# Runs substrate logic on host (macOS/Linux) without hardware.
# Usage: make test

CC ?= cc
CFLAGS = -Wall -Wextra -O2 -g -DREFLEX_RTC_DATA_ATTR= -DREFLEX_HOST_BUILD
INCLUDES = -I../../include -I../../vm/include -I../../kernel -I../stub_headers

SRCS = test_main.c \
       test_scheduler.c \
       test_kv.c \
       test_integration.c \
       mock_hal.c \
       mock_task.c \
       ../../vm/ternary.c \
       ../../kernel/reflex_sched.c \
       ../../platform/esp32c6/reflex_crypto_esp32c6.c

OUT = build/reflex_test

.PHONY: test clean

test: $(OUT)
	@./$(OUT)

$(OUT): $(SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS) -lm

clean:
	rm -rf build
