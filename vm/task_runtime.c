/** @file task_runtime.c
 * @brief VM task lifecycle and scheduling wrapper.
 */
#include "reflex_vm_task.h"

#include <string.h>

#include "reflex_types.h"
#include "reflex_task.h"

#include "reflex_fabric.h"
#include "reflex_vm.h"

#define REFLEX_VM_TASK_DEFAULT_STEPS 2
#define REFLEX_VM_TASK_DEFAULT_DELAY_MS 20
#define REFLEX_VM_TASK_DEFAULT_STACK 4096
#define REFLEX_VM_TASK_DEFAULT_PRIORITY 5

static void reflex_vm_task_apply_defaults(reflex_vm_task_config_t *config)
{
    if (config->steps_per_slice == 0) {
        config->steps_per_slice = REFLEX_VM_TASK_DEFAULT_STEPS;
    }
    if (config->delay_ms == 0) {
        config->delay_ms = REFLEX_VM_TASK_DEFAULT_DELAY_MS;
    }
    if (config->stack_size == 0) {
        config->stack_size = REFLEX_VM_TASK_DEFAULT_STACK;
    }
    if (config->priority == 0) {
        config->priority = REFLEX_VM_TASK_DEFAULT_PRIORITY;
    }
    if (config->name == NULL) {
        config->name = "reflex-vm";
    }
}

static void reflex_vm_task_entry(void *arg)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)arg;

    while (runtime->running) {
        if (runtime->vm.status == REFLEX_VM_STATUS_HALTED || runtime->vm.status == REFLEX_VM_STATUS_FAULTED) {
            runtime->running = false;
            break;
        }

        if (reflex_vm_run(&runtime->vm, runtime->config.steps_per_slice) == REFLEX_ERR_TIMEOUT) {
            reflex_task_delay_ms(runtime->config.delay_ms);
            continue;
        }

        if (runtime->vm.status == REFLEX_VM_STATUS_READY) {
            reflex_task_delay_ms(runtime->config.delay_ms);
        }
    }

    runtime->handle = NULL;
    reflex_task_delete(NULL);
}

void reflex_vm_task_runtime_init(reflex_vm_task_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

reflex_err_t reflex_vm_task_start(reflex_vm_task_runtime_t *runtime,
                               const reflex_vm_image_t *image,
                               const reflex_vm_task_config_t *config)
{
    reflex_vm_task_config_t effective_config;

    REFLEX_RETURN_ON_FALSE(runtime != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "runtime is required");
    REFLEX_RETURN_ON_FALSE(image != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "image is required");
    REFLEX_RETURN_ON_FALSE(!runtime->running, REFLEX_ERR_INVALID_STATE, "vm_task", "runtime already running");
    REFLEX_RETURN_ON_ERROR(reflex_vm_validate_image(image), "vm_task", "image validation failed");

    memset(&effective_config, 0, sizeof(effective_config));
    if (config != NULL) {
        effective_config = *config;
    }
    reflex_vm_task_apply_defaults(&effective_config);

    runtime->image = image;
    runtime->config = effective_config;
    runtime->running = true;
    runtime->vm.node_id = REFLEX_NODE_VM;
    reflex_vm_use_default_syscalls(&runtime->vm);
    REFLEX_RETURN_ON_ERROR(reflex_vm_load_image(&runtime->vm, image), "vm_task", "image load failed");

    if (reflex_task_create(reflex_vm_task_entry,
                           effective_config.name,
                           effective_config.stack_size,
                           runtime,
                           effective_config.priority,
                           &runtime->handle) != REFLEX_OK) {
        runtime->running = false;
        runtime->handle = NULL;
        return REFLEX_FAIL;
    }

    return REFLEX_OK;
}

reflex_err_t reflex_vm_task_start_binary(reflex_vm_task_runtime_t *runtime,
                                      const uint8_t *buffer,
                                      size_t len,
                                      const reflex_vm_task_config_t *config)
{
    reflex_vm_task_config_t effective_config;

    REFLEX_RETURN_ON_FALSE(runtime != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "runtime is required");
    REFLEX_RETURN_ON_FALSE(buffer != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "buffer is required");
    REFLEX_RETURN_ON_FALSE(len > 0, REFLEX_ERR_INVALID_ARG, "vm_task", "buffer length is required");
    REFLEX_RETURN_ON_FALSE(!runtime->running, REFLEX_ERR_INVALID_STATE, "vm_task", "runtime already running");

    memset(&effective_config, 0, sizeof(effective_config));
    if (config != NULL) {
        effective_config = *config;
    }
    reflex_vm_task_apply_defaults(&effective_config);

    runtime->image = NULL;
    runtime->config = effective_config;
    runtime->running = true;
    runtime->vm.node_id = REFLEX_NODE_VM;
    reflex_vm_use_default_syscalls(&runtime->vm);
    REFLEX_RETURN_ON_ERROR(reflex_vm_load_binary(&runtime->vm, buffer, len), "vm_task", "binary load failed");

    if (reflex_task_create(reflex_vm_task_entry,
                           effective_config.name,
                           effective_config.stack_size,
                           runtime,
                           effective_config.priority,
                           &runtime->handle) != REFLEX_OK) {
        runtime->running = false;
        runtime->handle = NULL;
        reflex_vm_unload(&runtime->vm);
        return REFLEX_FAIL;
    }

    return REFLEX_OK;
}

/* How long to wait for the VM task to notice `running == false` and retire.
 *
 * The task only re-checks the flag between slices, so the wait has to cover a
 * slice plus whatever the running program is doing — including a DELAY syscall
 * it issued itself. Two seconds is generous for the default 20 ms slice and
 * still bounded. */
#define REFLEX_VM_TASK_STOP_TIMEOUT_MS 2000
#define REFLEX_VM_TASK_STOP_POLL_MS    10

/* Yield window after the task retires, so the idle task can reclaim its TCB and
 * stack before a caller reuses the runtime. Longer than one tick at any
 * supported tick rate. */
#define REFLEX_VM_TASK_REAP_MS 20

reflex_err_t reflex_vm_task_stop(reflex_vm_task_runtime_t *runtime)
{
    REFLEX_RETURN_ON_FALSE(runtime != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "runtime is required");

    runtime->running = false;

    /* Bounded wait. This used to spin `while (runtime->handle != NULL)` with no
     * timeout, so anything that stopped the VM task from reaching the top of
     * its loop hung the caller permanently. A VM program only has to sit in a
     * DELAY syscall to cause it; before the negative-delay fix in
     * vm/syscall.c a single instruction could park the task for ~49 days.
     *
     * Reachability, re-checked on 2026-09-07 and no longer what this comment
     * used to say. It claimed the path was invoked only by
     * reflex_service_stop_all, that nothing calls that, and that it was absent
     * from reflex_os.elf entirely. That is now false in the running system:
     * reflex_service_watchdog_tick calls svc->stop followed by svc->start for
     * any service reporting FAULTED, and goose_supervisor_pulse calls the
     * watchdog every REFLEX_SUPERVISOR_WATCHDOG_DIV pulses — 1 Hz at the 10 Hz
     * supervisor. reflex_vm_task_service_status reports FAULTED whenever
     * vm.status is FAULTED, so this runs on every VM fault, not never.
     *
     * The bounded wait therefore matters: an unbounded spin here would have
     * hung the supervisor pulse, and with it the whole substrate.
     *
     * Timing out is reported rather than papered over: the task is still alive
     * and still owns its stack, which the caller needs to know. */
    uint32_t waited = 0;
    while (runtime->handle != NULL && waited < REFLEX_VM_TASK_STOP_TIMEOUT_MS) {
        reflex_task_delay_ms(REFLEX_VM_TASK_STOP_POLL_MS);
        waited += REFLEX_VM_TASK_STOP_POLL_MS;
    }

    if (runtime->handle != NULL) {
        REFLEX_LOG_ERROR_IMPL("vm_task", "%s: task did not retire within %u ms",
                              __func__, (unsigned)REFLEX_VM_TASK_STOP_TIMEOUT_MS);
        return REFLEX_ERR_TIMEOUT;
    }

    /* The handle clear says the entry function finished its loop; it does not
     * say the task's memory is back. reflex_vm_task_entry clears the handle and
     * then calls reflex_task_delete(NULL), and vTaskDelete on the *calling*
     * task defers the TCB and stack teardown to the idle task. The watchdog
     * restart path — stop() immediately followed by start() — therefore
     * observed a retired task and created its replacement while the previous
     * stack was still allocated, holding two VM stacks at once on a board with
     * no memory to spare for it.
     *
     * Yielding for longer than a tick lets the idle task, which is the lowest
     * priority runnable thing here, actually run and reap. This is a mitigation
     * and not a guarantee: under sustained load the idle task may still not
     * have run by the time this returns. Closing the window properly means not
     * self-deleting at all, which is a change to task teardown that should be
     * made against hardware rather than reasoned into place — see P0-M4 in
     * docs/implementation-status.md.
     *
     * Safe regardless of ordering, because the entry function touches nothing
     * in *runtime after clearing the handle, so a restart that overlaps a
     * not-yet-reaped task cannot corrupt the runtime it reuses. The cost is
     * memory, not correctness. */
    reflex_task_delay_ms(REFLEX_VM_TASK_REAP_MS);

    return REFLEX_OK;
}

bool reflex_vm_task_is_running(const reflex_vm_task_runtime_t *runtime)
{
    return runtime != NULL && runtime->running;
}

reflex_err_t reflex_vm_task_service_init(void *ctx)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)ctx;
    REFLEX_RETURN_ON_FALSE(runtime != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "runtime required");

    /* The soft cache is caller configuration, not runtime state, and must
     * survive this hook.
     *
     * reflex_service_register calls init synchronously, so a caller that
     * installs a cache and then registers the service — which is the only
     * sensible order, since registration is what makes the runtime live — had
     * the pointer zeroed by the memset inside runtime_init before the VM ever
     * ran. main.c did exactly that: the system VM was given a cache and then
     * silently demoted to direct-MMU mode, so every TLD/TST/TFLUSH/TINV cache
     * semantic was skipped with no diagnostic. Not unsafe (a NULL cache is a
     * supported mode) but a shipped feature that was never once active.
     *
     * Preserving it here rather than reordering main.c is deliberate: the
     * ordering fix would work today and break silently the next time someone
     * moved two adjacent lines. */
    struct reflex_cache *cache = runtime->vm.cache;
    reflex_vm_task_runtime_init(runtime);
    runtime->vm.cache = cache;
    return REFLEX_OK;
}

reflex_err_t reflex_vm_task_service_start(void *ctx)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)ctx;
    REFLEX_RETURN_ON_FALSE(runtime != NULL, REFLEX_ERR_INVALID_ARG, "vm_task", "runtime required");
    if (runtime->image == NULL) {
        return REFLEX_OK; // Or REFLEX_ERR_INVALID_STATE if we require an image
    }
    return reflex_vm_task_start(runtime, runtime->image, &runtime->config);
}

reflex_err_t reflex_vm_task_service_stop(void *ctx)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)ctx;
    return reflex_vm_task_stop(runtime);
}

reflex_service_status_t reflex_vm_task_service_status(void *ctx)
{
    reflex_vm_task_runtime_t *runtime = (reflex_vm_task_runtime_t *)ctx;
    if (runtime == NULL) return REFLEX_SERVICE_STATUS_STOPPED;
    
    if (runtime->vm.status == REFLEX_VM_STATUS_FAULTED) {
        return REFLEX_SERVICE_STATUS_FAULTED;
    }
    
    return (runtime->handle != NULL) ? REFLEX_SERVICE_STATUS_STARTED : REFLEX_SERVICE_STATUS_STOPPED;
}

reflex_err_t reflex_vm_task_register_service(reflex_vm_task_runtime_t *runtime, const char *name)
{
    static reflex_service_desc_t desc;
    desc.name = name;
    desc.init = reflex_vm_task_service_init;
    desc.start = reflex_vm_task_service_start;
    desc.stop = reflex_vm_task_service_stop;
    desc.status = reflex_vm_task_service_status;
    desc.context = runtime;
    return reflex_service_register(&desc);
}

reflex_err_t reflex_vm_task_self_check(void)
{
    static const reflex_vm_instruction_t program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 1, .imm = -1},
        {.opcode = REFLEX_VM_OPCODE_TADD, .dst = 2, .src_a = 0, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TCMP, .dst = 3, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TBRPOS, .src_a = 3, .imm = 6},
        {.opcode = REFLEX_VM_OPCODE_THALT},
        {.opcode = REFLEX_VM_OPCODE_TSUB, .dst = 4, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    static const reflex_vm_image_t image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = program,
        .instruction_count = sizeof(program) / sizeof(program[0]),
    };
    static reflex_vm_task_runtime_t runtime;
    static reflex_word18_t expected_one;
    uint32_t deadline = 100;

    reflex_vm_task_runtime_init(&runtime);
    REFLEX_RETURN_ON_ERROR(reflex_word18_from_int32(1, &expected_one), "vm_task", "failed to encode expected value");
    REFLEX_RETURN_ON_ERROR(reflex_vm_task_start(&runtime, &image, NULL), "vm_task", "failed to start runtime");

    while (deadline-- > 0 && reflex_vm_task_is_running(&runtime)) {
        reflex_task_delay_ms(10);
    }

    REFLEX_RETURN_ON_FALSE(runtime.vm.status == REFLEX_VM_STATUS_HALTED,
                        REFLEX_FAIL,
                        "vm_task",
                        "runtime must halt cleanly");
    REFLEX_RETURN_ON_FALSE(reflex_word18_equal(&runtime.vm.registers[4], &expected_one),
                        REFLEX_FAIL,
                        "vm_task",
                        "runtime result register mismatch");

    return reflex_vm_task_stop(&runtime);
}
