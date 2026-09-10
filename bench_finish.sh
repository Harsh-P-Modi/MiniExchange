#!/usr/bin/env bash
# =============================================================================
# bench_finish.sh  — collects ONLY the two still-missing pieces:
#   (1) Phase 8, 72-byte Order side  : harness from a 5168fb0 worktree
#       (that commit caps the bench pool at 4096; HEAD 164e03b does not and
#        stalls after "ADD (1 fill)").
#   (2) Phase 11 / R9 syscall trace  : bare-launch the server, then attach
#       `strace -f -e trace=...` to the live PID.  (`strace -ff -c` is
#       rejected on this box -> strace exits before exec -> "server failed
#       to start".)
#
# Prints RESULTS.md to the terminal at the end.  Run from inside the repo:
#     ./bench_finish.sh
# =============================================================================
set -uo pipefail
ROOT="$(git rev-parse --show-toplevel)"; cd "$ROOT"
TS="$(date +%Y%m%d-%H%M%S)"
OUT="$ROOT/bench-finish-$TS"; RAW="$OUT/raw"; mkdir -p "$RAW"
LOG="$OUT/run.log"; RES="$OUT/RESULTS.md"

log(){ printf '%s  %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$LOG" >&2; }
add(){ printf '%s\n' "$*" >> "$RES"; }
peek(){ log "---- RESULTS.md so far ----"; sed 's/^/    /' "$RES" >&2; log "---------------------------"; }

SINK=""
cleanup(){
  [ -n "$SINK" ] && kill "$SINK" 2>/dev/null || true
  pkill -9 -f 'exchange_server 9100' 2>/dev/null || true
  pkill -9 -f 'bench-finish.*udp_sink' 2>/dev/null || true
  git worktree remove /tmp/mx-p8-after   --force 2>/dev/null || true
  git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true
  log "cleanup done"
}
trap cleanup EXIT

# ---- 0. clear anything left from previous runs ----
pkill -9 -f 'exchange_server 9100' 2>/dev/null || true
git worktree remove /tmp/mx-p8-after   --force 2>/dev/null || true
git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true
sleep 1

add "# Phase 8 (72-byte) + R9 — finish run"
add ""
add "- **When:** $(date -Is)"
add "- **Host:** $(lscpu | sed -n 's/^Model name: *//p' | tr -s ' ')  |  **Kernel:** $(uname -sr)"
add "- **HEAD:** $(git rev-parse --short HEAD)"
add ""
log "OUT: $OUT"

# =====================================================================
log "PHASE 8 — 72-byte Order, harness from worktree 5168fb0"
# =====================================================================
git worktree add /tmp/mx-p8-after 5168fb0 2>&1 | tee -a "$LOG"
log "P8: configure (FetchContent download — a few minutes)"
stdbuf -oL -eL timeout 1800 cmake -S /tmp/mx-p8-after -B /tmp/mx-p8-after/build \
    -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >>"$LOG" 2>&1
log "P8: build benchmark_harness"
stdbuf -oL -eL timeout 1800 cmake --build /tmp/mx-p8-after/build --target benchmark_harness >>"$LOG" 2>&1

add "## Phase 8 — 72-byte Order (5168fb0, 4096-slot bench pool)"
add ""
if [ -x /tmp/mx-p8-after/build/benchmark_harness ]; then
  log "P8: run 72-byte harness"
  ( cd /tmp/mx-p8-after && stdbuf -oL -eL timeout 600 taskset -c 2,3 ./build/benchmark_harness ) \
      2>&1 | tee "$RAW/phase-08-after-72B.txt" | tee -a "$LOG"
  add '```text'
  grep -E 'ADD \(|CANCEL \(|Mixed|orders/sec' "$RAW/phase-08-after-72B.txt" >> "$RES" \
    || echo "(no rows — see raw/phase-08-after-72B.txt)" >> "$RES"
  add '```'
else
  log "P8: BUILD FAILED (see run.log)"
  add "**BUILD FAILED** — see run.log / raw"
fi
add ""
git worktree remove /tmp/mx-p8-after --force 2>/dev/null || true
peek

# =====================================================================
log "PHASE 11 / R9 — strace syscall trace (bare launch + attach)"
# =====================================================================
add "## Phase 11 / R9 — syscall trace"
add ""
add "\`strace -f\` attached to the live server PID. Load: 20000 framed"
add "plaintext orders, alternating SELL/BUY at one price (every 2nd = 1 fill"
add "-> trade -> feed \`sendto\`). Only call **counts** are meaningful under"
add "strace; those are exact."
add ""

if ! command -v strace >/dev/null 2>&1; then
  log "R9: installing strace"; sudo apt-get install -y strace >>"$LOG" 2>&1 || true
fi

if ! command -v strace >/dev/null 2>&1; then
  log "R9: strace unavailable — skipping"
  add "_strace unavailable — R9 skipped._"; add ""
else
  sudo sysctl -w kernel.yama.ptrace_scope=0 >/dev/null 2>&1 || true

  log "R9: build HEAD exchange_server"
  stdbuf -oL -eL timeout 900 cmake --build build --target exchange_server >>"$LOG" 2>&1

  log "R9: worktree 8598213 + build exchange_server"
  git worktree add /tmp/mx-p11-before 8598213 2>&1 | tee -a "$LOG"
  stdbuf -oL -eL timeout 1800 cmake -S /tmp/mx-p11-before -B /tmp/mx-p11-before/build \
      -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >>"$LOG" 2>&1
  stdbuf -oL -eL timeout 1800 cmake --build /tmp/mx-p11-before/build --target exchange_server >>"$LOG" 2>&1

  cat > "$OUT/udp_sink.py" <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 9001))
while True:
    s.recvfrom(65536)
PY
  cat > "$OUT/loadgen.py" <<'PY'
import socket, struct, sys
port, n = int(sys.argv[1]), int(sys.argv[2])
s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.settimeout(15)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
def rt(cmd):
    b = cmd.encode(); s.sendall(struct.pack(">I", len(b)) + b)
    h = b""
    while len(h) < 4: h += s.recv(4 - len(h))
    (ln,) = struct.unpack(">I", h); g = 0
    while g < ln: g += len(s.recv(ln - g))
for i in range(n):
    rt(f"ADD {i+1} SELL 100 10" if i % 2 == 0 else f"ADD {i+1} BUY 100 10")
print(f"ok {n}")
PY

  python3 "$OUT/udp_sink.py" & SINK=$!
  log "R9: udp_sink pid $SINK"

  r9(){  # $1 = label   $2 = server binary
    local L=$1 BIN=$2
    local strc="$RAW/strace-$L.log"  serr="$RAW/server-$L.log"
    if [ ! -x "$BIN" ]; then
      log "R9[$L]: $BIN missing"; add "### $L — server binary missing (build failed, see run.log)"; add ""; return 0
    fi
    pkill -9 -f 'exchange_server 9100' 2>/dev/null || true; sleep 1
    log "R9[$L]: launch  $BIN 9100 --protocol=plaintext"
    setsid taskset -c 2,3 "$BIN" 9100 --protocol=plaintext >"$serr" 2>&1 &
    local SRV=$!
    sleep 2
    if ! kill -0 "$SRV" 2>/dev/null; then
      log "R9[$L]: server exited immediately"
      add "### $L — server failed to start"; add ""; add '```'; cat "$serr" >> "$RES"; add '```'; add ""; return 0
    fi
    if ! python3 "$OUT/loadgen.py" 9100 2 >>"$LOG" 2>&1; then
      log "R9[$L]: sanity round-trip failed"
      add "### $L — server up but not answering framed plaintext"; add ""; add '```'; cat "$serr" >> "$RES"; add '```'; add ""
      kill -INT "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; return 0
    fi
    log "R9[$L]: attach strace + send 20000 orders"
    strace -f -qq -e signal=none \
      -e trace=sendto,write,writev,sched_yield,eventfd2,epoll_wait,epoll_pwait,futex,recvfrom \
      -o "$strc" -p "$SRV" 2>>"$LOG" &
    local ST=$!
    sleep 1
    stdbuf -oL timeout 600 taskset -c 4,5 python3 "$OUT/loadgen.py" 9100 20000 2>&1 | tee -a "$LOG"
    sleep 1
    kill -INT "$ST"  2>/dev/null; wait "$ST"  2>/dev/null
    kill -INT "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null
    log "R9[$L]: strace lines: $(wc -l < "$strc" 2>/dev/null || echo 0)"

    add "### $L — syscall count per thread"
    add ""
    add '```text'
    # strace -f prefixes each line with "[pid TID]" (or a bare "TID" on
    # some builds); the very first thread may be unprefixed = main.
    awk '
      { if (match($0, /^\[pid +[0-9]+\]/)) { t=substr($0,RSTART,RLENGTH); gsub(/[^0-9]/,"",t); rest=substr($0,RLENGTH+1) }
        else if ($1 ~ /^[0-9]+$/ && $2 ~ /\(/) { t=$1; rest=substr($0, length($1)+2) }
        else { t="main"; rest=$0 }
        if (match(rest, /^[a-z_0-9]+\(/)) { sc=substr(rest,RSTART,RLENGTH-1); print t, sc }
      }' "$strc" | sort | uniq -c | sort -k3,3 -k1,1nr \
        | awk '{printf "  %-13s tid=%-8s calls=%s\n", $3, $2, $1}' >> "$RES"
    add '```'
    add "_full: raw/strace-$L.log   server stdout: raw/server-$L.log_"
    add ""
  }

  r9 before /tmp/mx-p11-before/build/exchange_server
  peek
  r9 after  ./build/exchange_server

  kill "$SINK" 2>/dev/null; SINK=""
  git worktree remove /tmp/mx-p11-before --force 2>/dev/null || true
fi

cp "$RES" "$ROOT/RESULTS-finish-$TS.md"
log "DONE.  RESULTS: $RES   (copy at repo root: RESULTS-finish-$TS.md)"
echo; echo "================= RESULTS.md ================="; cat "$RES"
