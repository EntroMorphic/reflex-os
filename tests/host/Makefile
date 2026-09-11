# Reflex OS Host Test Runner
#
# Runs substrate logic on host (macOS/Linux) without hardware.
# Usage: make test

CC ?= cc
CFLAGS = -Wall -Wextra -O2 -g -DREFLEX_RTC_DATA_ATTR= -DREFLEX_HOST_BUILD
INCLUDES = -I../../include -I../../vm/include -I../../kernel -I../stub_headers -I../../components/goose/include -I../../shell/include -I../../core/include

SRCS = test_main.c \
       test_scheduler.c \
       test_kv.c \
       test_integration.c \
       test_policy.c \
       test_vm_regress.c \
       test_log_format.c \
       test_lattice.c \
       test_registry.c \
       test_shell_policy.c \
       test_shell_parse.c \
       test_shell_outcome.c \
       test_kqueue.c \
       test_heap.c \
       mock_hal.c \
       mock_task.c \
       ../../vm/ternary.c \
       ../../components/goose/goose_policy.c \
       ../../vm/cache.c \
       ../../vm/loader.c \
       ../../vm/task_runtime.c \
       ../../vm/mmu.c \
       ../../platform/reflex_log_format.c \
       ../../components/goose/goose_lattice.c \
       ../../components/goose/goose_registry.c \
       ../../shell/shell_policy.c \
       ../../shell/shell_parse.c \
       ../../shell/shell_outcome.c \
       ../../kernel/reflex_sched.c \
       ../../kernel/reflex_kqueue.c \
       ../../kernel/reflex_heap.c \
       ../../platform/reflex_crypto.c

OUT = build/reflex_test

.PHONY: test clean

test: $(OUT)
	@./$(OUT)

# Headers are prerequisites too.
#
# This rule listed only $(SRCS), so a change confined to a header did not
# trigger a rebuild and the suite silently re-ran the previous binary. Caught by
# mutating reflex_intr_priority_for — a static inline in a header — and watching
# the test that pins it report a pass. The same class of defect as a format gate
# that cannot fail: a check that does not run is indistinguishable from one that
# passes.
HDRS := $(shell find ../../include ../../vm/include ../../components/goose/include \
                    ../../shell/include ../../kernel ../../core/include \
                    ../stub_headers . -name '*.h' 2>/dev/null)

$(OUT): $(SRCS) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS) -lm

clean:
	rm -rf build
