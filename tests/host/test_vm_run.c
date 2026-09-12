/** @file test_vm_run.c
 * @brief The interpreter's run loop, on the host.
 *
 * `reflex_vm_run_bounded` gained a wall-clock budget so that `vm run` could not
 * hang the shell, and that budget was verified on hardware only: the host suite
 * stubbed `reflex_vm_run` to return REFLEX_OK, so nothing here executed a single
 * instruction. A bound nothing tests is a bound nobody can refactor safely.
 *
 * The budget is driven deterministically rather than by real elapsed time. The
 * mock HAL's clock only moves when something moves it, so a syscall handler
 * that advances it models precisely the case the budget exists for — one step
 * that blocks for a long time — without the test depending on how fast the
 * machine running it happens to be.
 */
#include <stdio.h>
#include <string.h>

#include "reflex_vm.h"
#include "reflex_types.h"
#include "reflex_hal.h"
#include "reflex_fabric.h"
#include "goose.h"

static int s_pass = 0;
static int s_fail = 0;

#define CHECK(name, expr)                                                                          \
    do {                                                                                           \
        if (expr) {                                                                                \
            s_pass++;                                                                              \
        } else {                                                                                   \
            printf("  FAIL: %s\n", name);                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* --- Link stubs ----------------------------------------------------------
 *
 * The interpreter names these from opcodes the programs below never execute
 * (TSEND/TRECV reach the fabric, TROUTE/TCELL reach the GOOSE runtime). They
 * satisfy the linker and are deliberately inert; a test that exercises those
 * opcodes must link the real fabric and runtime instead of these. */
reflex_err_t reflex_fabric_send(const reflex_message_t *msg) {
    (void)msg;
    return REFLEX_OK;
}
reflex_err_t reflex_fabric_recv(uint8_t node_id, reflex_message_t *out_msg) {
    (void)node_id;
    (void)out_msg;
    return REFLEX_FAIL;
}
goose_cell_t *goose_fabric_get_cell(const char *name) {
    (void)name;
    return NULL;
}
goose_cell_t *goose_fabric_get_cell_by_coord(reflex_tryte9_t coord) {
    (void)coord;
    return NULL;
}
reflex_err_t goose_apply_route(goose_route_t *route) {
    (void)route;
    return REFLEX_OK;
}

/* --- Fixtures ------------------------------------------------------------ */

#define VM_MEM_WORDS 32
static reflex_word18_t s_vm_mem[VM_MEM_WORDS];

static void vm_init(reflex_vm_state_t *vm, const reflex_vm_instruction_t *prog, size_t len) {
    memset(vm, 0, sizeof(*vm));
    memset(s_vm_mem, 0, sizeof(s_vm_mem));
    reflex_vm_mmu_init(&vm->mmu);
    reflex_vm_mmu_add_region(&vm->mmu, s_vm_mem, VM_MEM_WORDS, REFLEX_MEM_PRIVATE, 0);
    vm->program = prog;
    vm->program_length = len;
    vm->status = REFLEX_VM_STATUS_READY;
}

/* The mock HAL defines this but declares it in no header, so it is named here
 * rather than reached implicitly. */
extern void reflex_hal_advance_time(uint64_t us);

/* Every call costs the VM a millisecond of wall clock. */
#define SYSCALL_COST_US 1000u
static uint32_t s_syscall_calls;

static reflex_err_t slow_syscall(struct reflex_vm_state *vm, reflex_vm_syscall_t id,
                                 const reflex_word18_t *src_a, const reflex_word18_t *src_b,
                                 reflex_word18_t *out, void *context) {
    (void)vm;
    (void)id;
    (void)src_a;
    (void)src_b;
    (void)out;
    (void)context;
    s_syscall_calls++;
    reflex_hal_advance_time(SYSCALL_COST_US);
    return REFLEX_OK;
}

static const reflex_vm_instruction_t halting_prog[] = {
    {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
    {.opcode = REFLEX_VM_OPCODE_THALT},
};

static const reflex_vm_instruction_t nop_prog[] = {
    {.opcode = REFLEX_VM_OPCODE_TNOP},
    {.opcode = REFLEX_VM_OPCODE_TNOP},
    {.opcode = REFLEX_VM_OPCODE_TNOP},
    {.opcode = REFLEX_VM_OPCODE_TNOP},
};

/* Four blocking syscalls, then a clean halt. */
static const reflex_vm_instruction_t slow_prog[] = {
    {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 1, .src_a = 0, .imm = 0},
    {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 1, .src_a = 0, .imm = 0},
    {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 1, .src_a = 0, .imm = 0},
    {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 1, .src_a = 0, .imm = 0},
    {.opcode = REFLEX_VM_OPCODE_THALT},
};

/* --- Tests --------------------------------------------------------------- */

static void test_run_to_halt(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, halting_prog, sizeof(halting_prog) / sizeof(halting_prog[0]));

    CHECK("run reaches THALT", reflex_vm_run(&vm, 8) == REFLEX_OK);
    CHECK("status is HALTED", vm.status == REFLEX_VM_STATUS_HALTED);
    CHECK("every instruction was stepped", vm.steps_executed == 2);
}

static void test_step_budget_exhausted(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, nop_prog, sizeof(nop_prog) / sizeof(nop_prog[0]));

    /* Four instructions, two steps allowed: the loop must give up, not run on. */
    CHECK("exhausting max_steps reports timeout", reflex_vm_run(&vm, 2) == REFLEX_ERR_TIMEOUT);
    CHECK("it stopped at the step limit", vm.steps_executed == 2);
    CHECK("and did not halt", vm.status != REFLEX_VM_STATUS_HALTED);
}

static void test_zero_steps_refused(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, halting_prog, sizeof(halting_prog) / sizeof(halting_prog[0]));

    CHECK("max_steps of zero is refused", reflex_vm_run(&vm, 0) == REFLEX_ERR_INVALID_ARG);
    CHECK("nothing was executed", vm.steps_executed == 0);
}

static void test_invalid_state_refused(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, halting_prog, sizeof(halting_prog) / sizeof(halting_prog[0]));
    vm.program = NULL;

    CHECK("a vm with no program is refused", reflex_vm_run(&vm, 8) != REFLEX_OK);
    CHECK("null vm is refused", reflex_vm_run(NULL, 8) != REFLEX_OK);
}

/* The defect the budget exists to prevent: steps remain, but the wall clock is
 * gone. Without the budget this program runs to completion no matter how long
 * each step blocks. */
static void test_wall_clock_budget_fires(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, slow_prog, sizeof(slow_prog) / sizeof(slow_prog[0]));
    vm.syscall_handler = slow_syscall;
    s_syscall_calls = 0;

    /* Steps are not the constraint: 100 allowed, 5 needed. Time is. */
    reflex_err_t err = reflex_vm_run_bounded(&vm, 100, 2500);
    CHECK("budget exhaustion reports timeout", err == REFLEX_ERR_TIMEOUT);
    CHECK("it stopped early, with steps to spare", vm.steps_executed < 100);
    CHECK("it did not halt", vm.status != REFLEX_VM_STATUS_HALTED);
}

/* Documented as "returns within budget plus one step", because the check
 * follows the step that overran and a step cannot be interrupted. Pinned here
 * so the guarantee cannot quietly weaken into "plus two". */
static void test_budget_overruns_by_at_most_one_step(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, slow_prog, sizeof(slow_prog) / sizeof(slow_prog[0]));
    vm.syscall_handler = slow_syscall;
    s_syscall_calls = 0;

    /* One step costs 1000 us and the budget is 1000 us, so the first step
     * overruns it and the loop must return immediately after that one step. */
    CHECK("timeout", reflex_vm_run_bounded(&vm, 100, SYSCALL_COST_US) == REFLEX_ERR_TIMEOUT);
    CHECK("exactly one step ran past the budget", vm.steps_executed == 1);
    CHECK("the syscall ran once", s_syscall_calls == 1);
}

/* A zero budget means "no wall-clock bound", which is what plain
 * reflex_vm_run relies on. If zero were treated as "no time at all", every
 * unbounded run would fail on its first step. */
static void test_zero_budget_means_unbounded(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, slow_prog, sizeof(slow_prog) / sizeof(slow_prog[0]));
    vm.syscall_handler = slow_syscall;
    s_syscall_calls = 0;

    CHECK("zero budget runs to completion", reflex_vm_run_bounded(&vm, 100, 0) == REFLEX_OK);
    CHECK("status is HALTED", vm.status == REFLEX_VM_STATUS_HALTED);
    CHECK("all four syscalls ran despite the elapsed time", s_syscall_calls == 4);
}

/* A fault must stop the loop rather than be counted as a step and continued. */
static void test_fault_stops_the_loop(void) {
    reflex_vm_state_t vm;
    vm_init(&vm, slow_prog, sizeof(slow_prog) / sizeof(slow_prog[0]));
    vm.syscall_handler = NULL; /* TSYS with no handler is a fault */

    CHECK("a faulting step does not report success", reflex_vm_run(&vm, 100) != REFLEX_OK);
    CHECK("the vm is marked faulted", vm.status == REFLEX_VM_STATUS_FAULTED);
    CHECK("it stopped at the faulting step", vm.steps_executed == 1);
}

int test_vm_run(void) {
    printf("[vm-run]  ");
    test_run_to_halt();
    test_step_budget_exhausted();
    test_zero_steps_refused();
    test_invalid_state_refused();
    test_wall_clock_budget_fires();
    test_budget_overruns_by_at_most_one_step();
    test_zero_budget_means_unbounded();
    test_fault_stops_the_loop();
    printf("ok\n");
    return s_fail;
}

int test_vm_run_passed(void) {
    return s_pass;
}
