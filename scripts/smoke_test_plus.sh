#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

rm -f /tmp/highperf_plus_access.log /tmp/highperf_plus_error.log /tmp/highperf_plus_runtime.log
./build_resume/server \
  --host 127.0.0.1 \
  --port 8888 \
  --threads 4 \
  --resources ./resources \
  --access-log /tmp/highperf_plus_access.log \
  --error-log /tmp/highperf_plus_error.log \
  --idle-timeout-ms 1000 >/tmp/highperf_plus_runtime.log 2>&1 &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
echo "--- IDLE TEST ---"
python3 - <<'PY'
import socket
import time

s = socket.create_connection(("127.0.0.1", 8888))
time.sleep(2)
try:
    s.sendall(b"GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    print(s.recv(64).decode("utf-8", errors="ignore"))
finally:
    s.close()
PY

echo "--- METRICS ---"
curl --max-time 5 http://127.0.0.1:8888/metrics
echo
echo "--- ACCESS LOG ---"
tail -n 20 /tmp/highperf_plus_access.log
echo "--- ERROR LOG ---"
tail -n 20 /tmp/highperf_plus_error.log
