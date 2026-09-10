#!/usr/bin/env bash
# =============================================================================
# bench_remaining.sh  (v2)
#
# Collects the two benchmark numbers still missing from benchmarks/results/:
#   * Phase 8      : Order 64-byte (49d4a15) vs 72-byte (5168fb0) matching-path
#                    latency + throughput, same box, same harness config.
#   * Phase 11/R9  : before/after syscall trace of exchange_server
#                    (pre-Phase-11 8598213  vs  HEAD build), via `strace -ff -c`.
#
# v2 changes:
#   - Phase 8 "after" runs from a worktree at 5168fb0 (its harness caps the
#     bench pool at 4096; HEAD 164e03b still uses the 1,000,000-slot pool and
#     stalls after "ADD (1 fill)").  Both sides now run from clean worktrees;
#     the live working tree is never touched or rebuilt.
#   - R9 uses `strace -ff -c` instead of `perf trace` (which rejected the
#     option combo on this kernel and printed its help text).  strace is
#     launched as the PARENT of the server, so no ptrace_scope / perf paranoia
#     issues, and `-ff -c` writes one per-thread syscall-count table per TID.
#
# Run from anywhere inside the repo:   ./bench_remaining.sh
# Needs: cmake, ninja, gcc, python3, strace   (script apt-installs strace)
# =============================================================================
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"; cd "$ROOT"
TS="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$ROOT/bench-remaining-$TS"
RAW="$OUTDIR/raw"
LOG="$OUTDIR/run.log"
RESULTS="$OUTDIR/RESULTS.md"
mkdir -p "$RAW"

BEFORE_P8=49d4a15     # last commit with 64-byte Order (pre Phase 8)
AFTER_P8=5168fb0      # 72-byte Order + harness with bench pool capped at 4096
BEFORE_P11=8598213    # pre Phase 11 (feed sendto runs on the engine stack)
ORDERS_LAT=10000      # harness internal iteration count is fixed; informational
ORDERS_R9=20000       # strace slows syscalls a lot; 20k orders => ~10k trades

# ---- logging helpers ------------------------------------------------------
log(){  printf '%s  %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$LOG" >&2; }
sect(){ printf '\n%s\n========== %s ==========\n' "$(date +%H:%M:%S)" "$*" | tee -a "$LOG" >&2; }
add(){  printf '%s\n' "$*" >> "$RESULTS"; }
show_progress(){ log "---- RESULTS.md so far ----"; sed 's/^/    /' "$RESULTS" >&2; log "---------------------------"; }

# run <timeout-s> <rawfile> <cmd...> : line-buffered, time-bounded, tee'd
run(){
  local t=$1 rf=$2; shift 2
  log "RUN (max ${t}s): $*"
  stdbuf -oL -eL timeout "$t" "$@" 2>&1 | tee -a "$rf" "$LOG"
  local rc=${PIPESTATUS[0]}
  if   [ "$rc" -eq 0 ];   then log "OK    -> $rf"
  elif [ "$rc" -eq 124 ]; then log "TIMEOUT ${t}s -> $rf  (continuing)"
  else log "exit $rc -> $rf  (continuing)"; fi
  return 0
}

SINK=""
cleanup(){
  [ -n "$SINK" ] && kill "$SINK" 2>/dev/null || true
  pkill -INT -f 'exchange_server 9100' 2>/dev/null || true
  git worktree remove /tmp/mx-p8-before  --force 2>/dev/null || true
  git worktree remove /tmp/mx-p8-after   --force 2>/dev/null || true
  git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true
  log "cleanup done"
}
trap cleanup EXIT

# ---- header -------------------------------------------------------------
GOV="$(cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor 2>/dev/null || echo '?')"
NOTURBO="$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo '?')"
add "# Remaining benchmarks — Phase 8 + Phase 11/R9"
add ""
add "- **Started:** $(date -Is)"
add "- **Host:** $(lscpu | sed -n 's/^Model name: *//p' | tr -s ' ')"
add "- **Kernel:** $(uname -sr)"
add "- **HEAD:** $(git rev-parse --short HEAD)  ($(git log -1 --format=%s | cut -c1-50))"
add "- **Refs used:** before-P8=\`$BEFORE_P8\` after-P8=\`$AFTER_P8\` before-P11=\`$BEFORE_P11\`"
add "- **Pinning:** taskset -c 2,3 (R9 loadgen: -c 4,5)"
add "- **governor(cpu2):** $GOV   **no_turbo:** $NOTURBO"
add ""
log "OUTDIR:   $OUTDIR"
log "RESULTS:  $RESULTS  (updated after every step)"

# a worktree + optional pool-cap patch + build benchmark_harness
# prep_harness <ref> <path> <patch:yes|no>
prep_harness(){
  local ref=$1 path=$2 patch=$3
  git worktree remove "$path" --force 2>/dev/null || true
  if ! git worktree add "$path" "$ref" 2>&1 | tee -a "$LOG"; then
    log "worktree add failed for $ref"; return 1
  fi
  if [ "$patch" = yes ]; then
    sed -i 's/MatchingEngine engine;/MatchingEngine engine{NullEventSink::instance(), 4096};/' \
        "$path/apps/benchmark/latency_bench.cpp"
    log "patched pool cap in $path ($(grep -c 'instance(), 4096' "$path/apps/benchmark/latency_bench.cpp") sites)"
  fi
  run 1800 "$RAW/build-$(basename "$path")-configure.log" \
      cmake -S "$path" -B "$path/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  run 1800 "$RAW/build-$(basename "$path").log" \
      cmake --build "$path/build" --target benchmark_harness
  [ -x "$path/build/benchmark_harness" ]
}

# =============================================================================
sect "PHASE 8 — Order 64B ($BEFORE_P8) vs 72B ($AFTER_P8)"
# =============================================================================
add "## Phase 8 — \`Order\` 64 -> 72 bytes"
add ""
add "Both sides built and run from isolated worktrees. Signal rows:"
add "**ADD (10 fills)** / **ADD (100 fills)**. ADD (no match) & CANCEL should be flat."
add ""

for side in after before; do
  if [ "$side" = after ]; then ref=$AFTER_P8;  path=/tmp/mx-p8-after;  patch=no;  bytes=72
  else                         ref=$BEFORE_P8; path=/tmp/mx-p8-before; patch=yes; bytes=64
  fi
  log "Phase 8 [$side / ${bytes}B / $ref]: prepare harness"
  if prep_harness "$ref" "$path" "$patch"; then
    log "Phase 8 [$side]: run harness"
    rf="$RAW/phase-08-$side-${bytes}B.txt"
    ( cd "$path" && stdbuf -oL -eL timeout 900 taskset -c 2,3 ./build/benchmark_harness ) 2>&1 \
        | tee -a "$rf" "$LOG"
    add "### ${side^} — ${bytes}-byte Order ($ref)"
    add ""
    add '```text'
    grep -E 'ADD \(|CANCEL \(|Mixed|orders/sec' "$rf" >> "$RESULTS" \
      || echo "(no rows captured — see raw/$(basename "$rf"))" >> "$RESULTS"
    add '```'
    add ""
  else
    log "Phase 8 [$side]: harness build failed"
    add "### ${side^} — ${bytes}-byte Order ($ref): BUILD FAILED (see raw/build-$(basename "$path").log)"
    add ""
  fi
  git worktree remove "$path" --force 2>/dev/null || true
  show_progress
done

# =============================================================================
sect "PHASE 11 / R9 — syscall trace (strace -ff -c)"
# =============================================================================
add "## Phase 11 / R9 — syscall-count trace"
add ""
add "\`strace -ff -c\`, one per-thread count table per TID. Load: one TCP"
add "connection, $ORDERS_R9 framed plaintext orders alternating SELL/BUY at the"
add "same price (every 2nd order is a 1-fill -> trade -> feed \`sendto\`)."
add "UDP feed drained on localhost:9001. strace inflates *time* heavily —"
add "only the **call counts** are meaningful here, and those are exact."
add ""

if ! command -v strace >/dev/null 2>&1; then
  log "R9: installing strace"
  sudo apt-get update -qq && sudo apt-get install -y -qq strace || true
fi

if ! command -v strace >/dev/null 2>&1; then
  log "R9: strace unavailable — skipping"
  add "_strace unavailable — R9 skipped._"; add ""
else
  # build current server (after)
  run 900 "$RAW/build-server-after.log" cmake --build build --target exchange_server

  # build pre-Phase-11 server (before)
  git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true
  git worktree add /tmp/mx-p11-before "$BEFORE_P11" 2>&1 | tee -a "$LOG"
  run 1800 "$RAW/build-p11-before-configure.log" \
      cmake -S /tmp/mx-p11-before -B /tmp/mx-p11-before/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  run 1800 "$RAW/build-p11-before.log" \
      cmake --build /tmp/mx-p11-before/build --target exchange_server

  cat > "$OUTDIR/udp_sink.py" <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 9001))
while True:
    s.recvfrom(65536)
PY
  cat > "$OUTDIR/loadgen.py" <<'PY'
import socket, struct, sys
port, n = int(sys.argv[1]), int(sys.argv[2])
s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.settimeout(10)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
def rt(cmd):
    b = cmd.encode()
    s.sendall(struct.pack(">I", len(b)) + b)
    h = b""
    while len(h) < 4:
        h += s.recv(4 - len(h))
    (ln,) = struct.unpack(">I", h)
    g = 0
    while g < ln:
        g += len(s.recv(ln - g))
try:
    for i in range(n):
        rt(f"ADD {i+1} SELL 100 10" if i % 2 == 0 else f"ADD {i+1} BUY 100 10")
except socket.timeout:
    print(f"TIMEOUT after ~{i} orders", file=sys.stderr)
    sys.exit(1)
print(f"sent {n} orders ok")
PY

  python3 "$OUTDIR/udp_sink.py" & SINK=$!
  log "R9: udp_sink on :9001 (pid $SINK)"

  trace(){   # $1 = label   $2 = server binary
    local label=$1 bin=$2
    local pfx="$RAW/strace-$label"
    rm -f "$pfx".*
    if [ ! -x "$bin" ]; then
      log "R9 [$label]: $bin missing — skip"; add "### $label: server binary missing"; add ""; return 0
    fi
    log "R9 [$label]: launch server under strace -ff -c"
    stdbuf -oL strace -ff -c -o "$pfx" -- \
        taskset -c 2,3 "$bin" 9100 --protocol=plaintext >>"$LOG" 2>&1 &
    local ST=$!
    sleep 2
    if ! pgrep -f "$bin 9100" >/dev/null; then
      log "R9 [$label]: server not up — skip"; kill "$ST" 2>/dev/null
      add "### $label: server failed to start"; add ""; return 0
    fi
    log "R9 [$label]: send $ORDERS_R9 orders"
    stdbuf -oL timeout 600 taskset -c 4,5 python3 "$OUTDIR/loadgen.py" 9100 "$ORDERS_R9" 2>&1 | tee -a "$LOG"
    sleep 1
    pkill -INT -f "$bin 9100" 2>/dev/null
    wait "$ST" 2>/dev/null
    log "R9 [$label]: strace files: $(ls "$pfx".* 2>/dev/null | wc -l)"

    add "### $label — per-thread syscall counts"
    add ""
    add '```text'
    for f in "$pfx".*; do
      [ -e "$f" ] || continue
      {
        echo "--- ${f##*/}"
        grep -E '(^|[[:space:]])(sendto|write|writev|sched_yield|futex|epoll_wait|epoll_pwait|nanosleep|recvfrom)([[:space:]]|$)' "$f" \
          | sed 's/^/    /'
        grep -E '[[:space:]]total$' "$f" | sed 's/^/    (total) /'
        echo
      } >> "$RESULTS"
    done
    add '```'
    add ""
  }

  trace before /tmp/mx-p11-before/build/exchange_server
  show_progress
  trace after  ./build/exchange_server

  kill "$SINK" 2>/dev/null; SINK=""
  git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true

  add "### Reading it"
  add ""
  add "Match the busiest thread (highest total syscalls) in each dump — that's"
  add "the engine/order-processing thread. **before**: it issues \`sendto\` ~="
  add "trade count. **after** (R5): its \`sendto\` is **0** — the same \`sendto\`"
  add "volume appears on a separate (feed-publisher) thread. **after** (R6): its"
  add "eventfd \`write\` count per filled order is lower. Full tables:"
  add "raw/strace-before.*  raw/strace-after.*"
  add ""
fi

# =============================================================================
sect "DONE"
# =============================================================================
add "- **Finished:** $(date -Is)"
cp "$RESULTS" "$ROOT/BENCHMARKS-remaining-$TS.md"
log "ALL DONE"
log "RESULTS.md          : $RESULTS"
log "copy at repo root   : $ROOT/BENCHMARKS-remaining-$TS.md"
log "raw artifacts       : $RAW"
show_progress
