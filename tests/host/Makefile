# Reflex OS Host Test Runner
#
# Runs substrate logic on host (macOS/Linux) without hardware.
# Usage: make test

CC ?= cc
CFLAGS = -Wall -Wextra -O2 -g -DREFLEX_RTC_DATA_ATTR= -DREFLEX_HOST_BUILD
# The real headers, not stand-ins.
#
# tests/stub_headers/ held four files under ONE shared include guard, three
# of them byte-identical, so including any one silently disabled the others
# and the suite compiled only because the superset was always reached first.
# Worse, its goose_route_t was a hand-written layout stand-in, so host tests
# ran against a reflex_vm_state_t shaped differently from the firmware's.
# Every source here compiles against the real headers, so it uses them.
INCLUDES = -I../../include -I../../vm/include -I../../kernel -I../../components/goose/include -I../../shell/include -I../../core/include -I../../storage/include

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
       test_vm_run.c \
       mock_hal.c \
       mock_task.c \
       ../../vm/ternary.c \
       ../../vm/interpreter.c \
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
                    ../../storage/include . -name '*.h' 2>/dev/null)

# .c files that a test source #includes directly.
#
# They cannot go in SRCS — that would compile them twice — and they are not
# headers, so neither existing prerequisite list covers them. Without this,
# editing platform/esp32c6/reflex_kv_flash.c does not rebuild the suite and
# `make test` cheerfully validates a stale binary.
#
# That is not hypothetical: it happened twice in one session, and both times it
# produced a clean pass for a defect that was sitting in the tree. It is the
# same class as the gate that cannot fail described above — a check run against
# yesterday's code is indistinguishable from one that passes. Derived rather
# than listed, so a test that starts including a different .c is covered
# without anyone remembering to come back here.
INCLUDED_SRCS := $(shell grep -ho '\#include "[^"]*\.c"' *.c 2>/dev/null \
                          | sed 's/.*"\(.*\)"/\1/' | sort -u)

$(OUT): $(SRCS) $(HDRS) $(INCLUDED_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS) -lm

clean:
	rm -rf build
