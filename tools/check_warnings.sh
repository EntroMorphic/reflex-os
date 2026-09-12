#!/usr/bin/env bash
#
# Host warning gate for the ESP-independent firmware sources.
#
# Compiles for real, at -O2, with -Wall -Wextra -Werror — stricter than the
# flags ESP-IDF itself uses. It has already earned its place: it found a second
# missing <stdlib.h> (an implicit malloc declaration, which returns int and
# truncates the pointer), an ignored dest_mac parameter that turned out to
# document a false contract in the radio header, and a file that passes
# -fsyntax-only but fails at assembly.
#
# What it does NOT do, stated plainly so nobody trusts it further than it goes:
# it does not reproduce the ESP-IDF toolchain's diagnostics. The defect that
# motivated writing it — a provable snprintf truncation — is reported by the
# RISC-V toolchain under -Werror=format-truncation and is missed by host GCC
# 11, 12, 13 and 14, at every optimisation level and with _FORTIFY_SOURCE on or
# off. That was measured, not assumed. A green run here is therefore weaker
# evidence than `make idf-build`, which is the only local check that speaks for
# the firmware build and which does catch it.
#
# Scope: the subset of the firmware that compiles against nothing but this
# repository's own headers — a direct consequence of the platform abstraction,
# and it grows as ESP-IDF dependencies are retired. Replacing the ESP-IDF soc
# register headers with the SVD-generated reflex_soc_esp32c6.h moved
# kernel/reflex_sched.c into this set on its own. Files still needing ESP-IDF
# are listed as skipped rather than quietly dropped.
#
# The rule this all exists to enforce: a syntax check is not a build.
# `gcc -fsyntax-only` generates no code and so runs none of the flow-sensitive
# analyses. Do not push firmware changes on the strength of one.
#
# Usage: tools/check_warnings.sh   (or: make warn-check)

set -uo pipefail
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
INC="-Iinclude -Icomponents/goose/include -Ivm/include -Ishell/include -Ikernel
     -Icore/include -Idrivers/include -Istorage/include -Iservices/include -Inet/include"

# -Wall -Wextra -Werror, at -O2 so the flow-sensitive analyses actually run.
#
# The two pointer/integer cast warnings are suppressed, in both directions.
# Firmware moves between uint32_t and pointers deliberately: MMIO addresses are
# uint32_t, and the scheduler computes a stack top as an integer. Both casts are
# exact on a 32-bit target and only lossy on the 64-bit host doing the
# compiling, so the warnings describe this machine rather than the firmware.
# Nothing else is suppressed.
# -Wno-asm-operand-widths belongs to the same family as the two casts above and
# is here for the same reason: a uint32_t in an "=r" constraint is exactly a
# register on RV32 and half of one on the 64-bit host doing the compiling, so
# the warning describes this machine and not the firmware. It fires on the
# scheduler's stack switch and on the kernel self-test's CSR reads.
FLAGS="-O2 -Wall -Wextra -Werror -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
       -Wno-asm-operand-widths \
       -DREFLEX_RTC_DATA_ATTR="

# Not compilable on the host for reasons other than a missing ESP-IDF header,
# so they cannot be detected by the probe below and are named explicitly.
# A case statement rather than an associative array, because macOS still ships
# bash 3.2 and `declare -A` is bash 4. Under 3.2 the declaration below parsed
# as an *indexed* array assignment, evaluated "core/boot.c" as an arithmetic
# expression, and died with `core: unbound variable` — printed on every run,
# and ignored, because the gate went on to exit 0 anyway. The skip list has
# therefore been doing nothing on this machine.
explicit_skip_reason() {
  case "$1" in
    core/boot.c)
      echo "needs sdkconfig.h for CONFIG_IDF_TARGET" ;;
    kernel/reflex_startup.c)
      echo "asks the compiler for RISC-V register t0; rejected by the host backend, not the assembler" ;;
    *)
      echo "" ;;
  esac
}

checked=0; skipped=0; failed=0
skip_list=(); fail_list=()

while IFS= read -r f; do
    reason="$(explicit_skip_reason "$f")"
    if [[ -n "$reason" ]]; then
        skip_list+=("$f — $reason"); skipped=$((skipped+1)); continue
    fi

    # Probe: a missing header means the file belongs to the ESP-IDF-dependent
    # set. Any other diagnostic is a real finding and must not be skipped.
    # Both compilers' wording, because they differ and only one was matched:
    # GCC says "fatal error: esp_now.h: No such file or directory", clang says
    # "fatal error: 'esp_now.h' file not found". This gate was written against
    # GCC, so on a clang host every ESP-IDF-dependent file fell through to be
    # compiled and counted as a failure. Extraction is sed rather than
    # `grep -oP`, which is a GNU extension BSD grep does not have.
    probe=$($CC -fsyntax-only $INC -DREFLEX_RTC_DATA_ATTR= "$f" 2>&1)
    if grep -qE "fatal error:.*(No such file or directory|file not found)" <<<"$probe"; then
        hdr=$(grep -m1 "fatal error:" <<<"$probe" \
              | sed -E "s/.*fatal error: '?([^':]+)'?.*/\1/")
        skip_list+=("$f — needs $hdr"); skipped=$((skipped+1)); continue
    fi

    # -S, not -c: stop after code generation and before the assembler. Every
    # warning is produced by the compiler proper, so nothing is lost, while
    # files carrying RISC-V inline assembly (reflex_sched.c, reflex_trap.c)
    # become checkable on an x86 host instead of failing on `wfi` and `csrci`.
    if ! out=$($CC -S $FLAGS $INC "$f" -o /dev/null 2>&1); then
        # A host backend that cannot assemble RISC-V inline asm is a fact about
        # the host, not a finding about the firmware. The comment above about
        # -S stopping before the assembler holds for GCC; clang validates
        # inline asm through its integrated assembler even under -S, so on a
        # clang host these files cannot be compiled at all. Detected rather
        # than named, so the GCC CI host keeps compiling them and only the
        # host that genuinely cannot skips them.
        if grep -qE "unrecognized instruction mnemonic|unknown register name|invalid operand for instruction|instantiated into assembly here" <<<"$out"; then
            skip_list+=("$f — RISC-V inline asm this host's backend cannot assemble")
            skipped=$((skipped+1)); continue
        fi
        fail_list+=("$out"); failed=$((failed+1))
    fi
    checked=$((checked+1))
done < <(find components core vm kernel platform storage services net drivers main \
              -name '*.c' ! -name 'goose_shadow_atlas.c' | sort)

echo "Warning gate: $checked compiled, $skipped skipped, $failed failed  [$($CC -dumpversion)]"

if (( skipped )); then
    echo
    echo "Skipped (need ESP-IDF; covered by the build-* CI jobs):"
    printf '  %s\n' "${skip_list[@]}"
fi

if (( failed )); then
    echo
    echo "FAILURES:"
    printf '%s\n' "${fail_list[@]}"
    exit 1
fi

echo "No warnings in the ESP-independent firmware."
