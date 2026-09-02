#!/usr/bin/env bash
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.
#
# runtests.sh - boot a KTEST=1 AV6 kernel and judge the KTRF it prints
#
# AV6 never halts, so the kernel's own "done" line is the completion marker
# rather than the exit of qemu.
#
#   ktest: ktrf 1                       format and version
#   ktest: count 5                      how many results follow
#   ktest: run 3 kmsg_wrap              emitted BEFORE the case
#   ktest: run 1 panic_wedge noreturn   no result is coming
#   ktest: expect 1 PANIC: 1 CPUs       what the log must contain instead
#   ktest: pass 3 kmsg_wrap checks=9 us=41
#   ktest: bad 3 file.c:71 detail       one per failed check
#   ktest: done count=5 pass=5 ...      the run finished
#
# A count with no matching done means the run was cut short, and the last
# "run" line names the case that did it. That is why run comes first.
#
# Usage:
#
#   tools/runtests.sh [--scenario NAME] <kernel.bin> [cpus] [mem_GiB]
#   make check
#
# Exit: 0 everything passed
#       1 the kernel worked and a test failed
#       2 the kernel never got a fair run (bad image, missing qemu, hang,
#         crash before the tests)
#
# Note:
#   --scenario NAME makes the kernel panic.

set -u -o pipefail

BOLD=$'\033[1m'; RED=$'\033[31m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RST=$'\033[0m';

QEMU=${QEMU:-qemu-system-aarch64}
DEADLINE=100                # 100 * 0.1s : POLLING
BANNER='AV6 \[AArch64\] booting'

SCENARIO=""
BIN=""
CPUS=4
MEM=1
LOG=""
QEMU_PID=""

declare -a FAILURES=()
declare -a BROKEN=()                # harness problem, not test failures

log() { printf '%s\n' "$*"; }
info() { log "${BOLD}[ktest]${RST} $*"; }
ok() { log "${GRN}[ OK ]${RST} $*"; }
warn() { log "${YEL}[WARN]${RST} $*"; }
err() { log "${RED}[FAIL]${RST} $*"; }

fail() { FAILURES+=("$1"); err "$1"; }      # the code is wrong
broke() { BROKEN+=("$1"); err "$1"; }       # the run was not fair
die() { broke "$1"; verdict; }

evidence() { grep -v '^ktest: ' "$LOG" || true; }

usage() { sed -e '1d' -e '/^[^#]/,$d' -e 's/^# \{0,1\}//' "$0"; }

cleanup() {
    [[ -n $QEMU_PID ]] && kill "$QEMU_PID" 2>/dev/null
    return 0
}
trap cleanup EXIT
trap 'err "interrupted"; exit 130' INT TERM

verdict() {
    echo
    if [[ ${#BROKEN[@]} -gt 0 ]]; then
        err "${#BROKEN[@]} harness problem(s):"
        printf '    %s\n' "${BROKEN[@]}"
        [[ -n $LOG && -f $LOG ]] && log "    log: $LOG"
        exit 2
    fi
    if [[ ${#FAILURES[@]} -gt 0 ]]; then
        err "${#FAILURES[@]} failure(s):"
        printf '    %s\n' "${FAILURES[@]}"
        log "    log: $LOG"
        exit 1
    fi
    exit 0
}

# arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --scenario) SCENARIO="${2:?--scenario needs a name}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown flag: $1 (see --help)" ;;
        *) break ;;
    esac
done

BIN="${1:?usage: $0 [--scenario NAME] <kernel.bin> [cpus] [mem_GiB]}"
CPUS="${2:-4}"
MEM="${3:-1}"

# preflight
command -v "$QEMU" >/dev/null || die "$QEMU not found in PATH"
[[ -f $BIN ]] || die "kernel image not found: $BIN"
[[ -s $BIN ]] || die "kernel image is empty: $BIN"

newest=$(find "$(dirname "$0")/../src" -name '*.[chs]' -newer "$BIN" -print -quit 2>/dev/null)
[[ -z $newest ]] || die "kernel image is older than $newest : the build did not complete"

"$QEMU" -machine help 2>/dev/null | grep -q '^virt ' \
    || die "$QEMU has no 'virt' machine"
"$QEMU" -cpu help 2>/dev/null | grep -qw 'neoverse-n2' \
    || die "$QEMU has no 'neoverse-n2' cpu"

LOG="${BIN%.bin}".ktrf.log

# boot
{
    echo "# runtests $(date -Is)"
    echo "# qemu: $("$QEMU" -version | head -1)"
    echo "# image: $BIN ($(stat -c '%s bytes, %y' "$BIN"))"
    echo "# smp=$CPUS mem=${MEM}G scenario=${SCENARIO:-none}"
} >"$LOG"

info "booting $BIN (smp=$CPUS mem=${MEM}G)${SCENARIO:+ scenario=$SCENARIO}"

"$QEMU" -M virt,mte=on,gic-version=3 -cpu neoverse-n2 \
    -smp "$CPUS" -m "${MEM}G" -nographic -kernel "$BIN" \
    >>"$LOG" 2>&1 </dev/null &
QEMU_PID=$!

alive=1
seen=0
size=0
for ((i = 0; i < DEADLINE; i++)); do
    if [[ -n $SCENARIO ]]; then
        if grep -q '^PANIC' "$LOG" 2>/dev/null; then
            now=$(stat -c %s "$LOG")
            [[ $seen -eq 1 && $now -eq $size ]] && break
            seen=1
            size=$now
        fi
    else
        grep -q '^ktest: done ' "$LOG" 2>/dev/null && break
    fi
    kill -0 "$QEMU_PID" 2>/dev/null || { alive=0; break; }
    sleep 0.1
done

cleanup
wait "$QEMU_PID" 2>/dev/null
QEMU_PID=""

grep -qE "$BANNER" "$LOG" || die "kernel never reached its boot banner - qemu problem"

if ! grep -q '^ktest: ktrf ' "$LOG"; then
    if evidence | grep -q 'PANIC'; then
        die "kernel panicked before the tests ran"
    fi
    die "no KTRF output : is $BIN built with KTEST=1?"
fi

grep -E '^ktest: (pass|fail|skip|bad) ' "$LOG" || true

# scenario
if [[ -n $SCENARIO ]]; then
    grep -q "^ktest: run [0-9]* $SCENARIO noreturn\$" "$LOG" \
        || fail "scenario $SCENARIO never started, or is not a KTEST_NORETURN case"
    ! grep -q '^ktest: done ' "$LOG" \
        || fail "scenario $SCENARIO returned"

    declare -a WANTED=()
    while IFS= read -r want; do
        WANTED+=("$want")
        evidence | grep -Fq "$want" || fail "expected in the log: $want"
    done < <(sed -n 's/^ktest: expect [0-9]* //p' "$LOG")

    panics=$(evidence | grep -c 'PANIC (cpu')
    [[ $panics -eq 1 ]] || fail "$panics CPUs panicked, expected 1"

    while IFS= read -r line; do
        for want in "${WANTED[@]}"; do
            [[ $line == *"$want"* ]] && continue 2
        done
        fail "undeclared distress: $line"
    done < <(evidence | grep -E 'unhandled|did not stop')

    echo
    info "panic output, for reading:"
    evidence | grep -E 'PANIC' | sed 's/^/    /'
    [[ ${#FAILURES[@]} -eq 0 ]] && ok "scenario $SCENARIO behaved"
    verdict
fi

done_line=$(grep '^ktest: done ' "$LOG" || true)
if [[ -z $done_line ]]; then
    last=$(grep '^ktest: run ' "$LOG" | tail -1)
    if [[ $alive -eq 1 ]]; then
        fail "kernel hung${last:+ in ${last#ktest: run }}"
    else
        fail "kernel stopped early${last:+ in ${last#ktest: run }}"
    fi
    verdict
fi

promised=$(sed -n 's/^ktest: count \([0-9]*\).*/\1/p' "$LOG")
reported=${done_line#*count=}
reported=${reported%% *}
passed=${done_line#*pass=}
passed=${passed%% *}
failed=${done_line#*fail=}
failed=${failed%% *}
skipped=${done_line#*skip=}
skipped=${skipped%% *}

[[ $promised == "$reported" ]] \
    || fail "promised $promised cases, reported $reported - the runner lost some"
[[ $((passed + failed + skipped)) -eq $reported ]]  \
    || fail "$passed + $failed + $skipped does not add up to $reported"
[[ $failed == 0 ]] \
    || fail "$failed of $reported cases failed"
[[ $reported -gt 0 || -n ${KTEST_ONLY:-} ]] \
    || fail "zero cases ran: nothing registered in .ktest"
[[ $skipped -eq 0 ]] || warn "$skipped case(s) skipped"

bad_lines=$(grep -c '^ktest: bad ' "$LOG")
[[ $bad_lines -eq 0 || $failed != 0 ]]  \
    || fail "$bad_lines bad line(s) but done says fail=0 : the runner is not reporting its own failures"


expect=1
while read -r verb idx _; do
    case "$verb" in
        run) [[ $idx == "$expect" ]] || fail "expected run $expect, got run $idx" ;;
        pass|fail|skip) expect=$((expect + 1)) ;;
    esac
done < <(sed -n 's/^ktest: \(run\|pass\|fail\|skip\) /\1 /p' "$LOG")

if evidence | grep -qE 'unhandled|did not stop'; then
    fail "kernel logged distress during the run:"
    evidence | grep -nE 'unhandled|did not stop' | sed 's/^/        /'
fi

[[ ${#FAILURES[@]} -eq 0 && ${#BROKEN[@]} -eq 0 ]] && ok "$passed of $reported cases passed"
verdict
