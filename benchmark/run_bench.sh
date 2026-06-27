#!/usr/bin/env bash
# =============================================================================
# benchmark/run_bench.sh
#
# Automated benchmark suite for the C++ HTTP Server.
# Compares thread counts and measures throughput / latency.
#
# Usage (run from project root):
#   chmod +x benchmark/run_bench.sh
#   ./benchmark/run_bench.sh [port] [requests] [concurrency]
#
# Examples:
#   ./benchmark/run_bench.sh                  # defaults: 8090, 5000 req, c=20
#   ./benchmark/run_bench.sh 9000 20000 50
#
# Dependencies:
#   - ab (ApacheBench) — ships with macOS, linux: apt install apache2-utils
#   - wrk (optional)  — brew install wrk
# =============================================================================
set -euo pipefail

PORT=${1:-8090}
REQUESTS=${2:-5000}
CONCURRENCY=${3:-20}
BINARY="./build/http_server"
TMPDIR_BM="/tmp/http_bench_$$"
mkdir -p "$TMPDIR_BM"

# ── Pre-flight ───────────────────────────────────────────────────────────────
if [[ ! -f "$BINARY" ]]; then
  echo "  Binary not found: $BINARY — run: cmake --build build"
  exit 1
fi
if ! command -v ab &>/dev/null; then
  echo "  'ab' not found. macOS: ships with Xcode. Linux: apt install apache2-utils"
  exit 1
fi

SERVER_PID=""

start_server() {
  local threads=$1
  "$BINARY" "$PORT" "$threads" &>/dev/null &
  SERVER_PID=$!
  sleep 0.8
  # Prime the LRU cache
  curl -s "http://127.0.0.1:${PORT}/" > /dev/null 2>&1 || true
}

stop_server() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
    sleep 0.5
  fi
}

# Collect one ab run, write results to a file
run_one() {
  local threads=$1
  local outfile="$TMPDIR_BM/result_${threads}.txt"

  start_server "$threads"

  ab -n "$REQUESTS" -c "$CONCURRENCY" -r \
     "http://127.0.0.1:${PORT}/" > "$outfile" 2>&1

  # Also capture /metrics
  curl -s "http://127.0.0.1:${PORT}/metrics" > "$TMPDIR_BM/metrics_${threads}.json" 2>/dev/null || true

  stop_server
}

# ── Banner ───────────────────────────────────────────────────────────────────
printf "\n"
printf "╔══════════════════════════════════════════════════════════════╗\n"
printf "║           C++ HTTP Server — Benchmark Suite (Phase 6)       ║\n"
printf "╠══════════════════════════════════════════════════════════════╣\n"
printf "║  Requests: %-8s  Concurrency: %-8s  Port: %-6s      ║\n" \
  "$REQUESTS" "$CONCURRENCY" "$PORT"
printf "╠══════════════════════════════════════════════════════════════╣\n"
printf "║  I/O Model : kqueue (macOS) / blocking accept (Linux)       ║\n"
printf "║  Cache     : LRU 1024 entries, files <5MB cached in RAM     ║\n"
printf "║  Logging   : async background writer (non-blocking hot path)║\n"
printf "╚══════════════════════════════════════════════════════════════╝\n\n"

# ── Run benchmarks ──────────────────────────────────────────────────────────
for T in 1 2 4 8; do
  printf "  Running with %d thread(s)...\n" "$T"
  run_one "$T"
done

# ── Results table ─────────────────────────────────────────────────────────────
printf "\n"
printf "╔═══════════╤════════════════╤═══════════════╤══════════╗\n"
printf "║  Threads  │   Req / sec    │  Avg Lat (ms) │  Failed  ║\n"
printf "╠═══════════╪════════════════╪═══════════════╪══════════╣\n"

for T in 1 2 4 8; do
  f="$TMPDIR_BM/result_${T}.txt"
  if [[ -f "$f" ]]; then
    rps=$(grep "Requests per second" "$f" | awk '{print $4}')
    lat=$(grep "Time per request" "$f" | head -1 | awk '{print $4}')
    fail=$(grep "Failed requests" "$f" | awk '{print $3}')
  else
    rps="N/A"; lat="N/A"; fail="N/A"
  fi

  # Cache hit rate from metrics
  mf="$TMPDIR_BM/metrics_${T}.json"
  hit_rate="?"
  if [[ -f "$mf" ]]; then
    hits=$(grep -o '"cache_hits": [0-9]*' "$mf" | grep -o '[0-9]*' || echo "0")
    total=$(grep -o '"total_requests": [0-9]*' "$mf" | grep -o '[0-9]*' || echo "0")
    if [[ "$total" -gt 0 ]]; then
      hit_rate="${hits}/${total}"
    fi
  fi

  printf "║  %-9s │  %-14s │  %-13s │  %-8s║\n" \
    "${T}t" "${rps:-?}" "${lat:-?}" "${fail:-?}"
done

printf "╚═══════════╧════════════════╧═══════════════╧══════════╝\n"

echo ""
echo "Results written to: $TMPDIR_BM"
echo "Log file:           ./server.log (if server was started from build/)"
echo ""

# ── Optional wrk ─────────────────────────────────────────────────────────────
if command -v wrk &>/dev/null; then
  echo "── wrk sustained throughput (30s, 4 threads, 100 connections) ──"
  start_server 8
  wrk -t4 -c100 -d30s "http://127.0.0.1:${PORT}/" || true
  stop_server
else
  printf "Tip: brew install wrk && wrk -t4 -c100 -d30s http://127.0.0.1:%s/\n" "$PORT"
fi

echo ""
echo "Benchmark complete."
rm -rf "$TMPDIR_BM"
