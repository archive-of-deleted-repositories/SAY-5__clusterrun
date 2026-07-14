#!/usr/bin/env bash
# End-to-end check of the crun client against a real controller and worker on
# LocalStack: submit through the control endpoint, watch to the terminal
# state, and confirm the exit-code contract for both success and dead-letter.
set -euo pipefail

ENDPOINT="${AWS_ENDPOINT_URL:-http://localhost:4566}"
export AWS_ENDPOINT_URL="$ENDPOINT"
BUILD="${BUILD_DIR:-build}"
CTRL="$BUILD/src/controller/clusterrun_controller"
WORKER="$BUILD/src/worker/clusterrun_worker"
CRUN="${CRUN_BIN:-$BUILD/crun}"
PORT="${CLUSTERRUN_CONTROL_PORT:-8447}"
export CLUSTERRUN_CONTROL_PORT="$PORT"
export CRUN_CONTROLLER="http://127.0.0.1:$PORT"

cleanup() {
  [[ -n "${CTRL_PID:-}" ]] && kill "$CTRL_PID" 2>/dev/null || true
  [[ -n "${WORKER_PID:-}" ]] && kill "$WORKER_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "starting controller and worker"
"$CTRL" &
CTRL_PID=$!
CLUSTERRUN_WORKER_ID=1 CLUSTERRUN_CPU=8 CLUSTERRUN_MEM_GB=16 CLUSTERRUN_NODE_CLASS=local \
  "$WORKER" &
WORKER_PID=$!

for _ in $(seq 1 30); do
  if curl -sf "$CRUN_CONTROLLER/v1/healthz" >/dev/null; then break; fi
  sleep 1
done
curl -sf "$CRUN_CONTROLLER/v1/healthz"
echo

TMP=$(mktemp -d)
printf '{"job_id":1,"command":["sh","-c","echo crun e2e"],"stdout_s3":"s3://results/e2e-1.out","attempts_max":2}' \
  > "$TMP/ok.json"
printf '{"job_id":2,"command":["sh","-c","exit 3"],"attempts_max":1}' \
  > "$TMP/fail.json"

echo "submitting and watching a passing job"
"$CRUN" submit "$TMP/ok.json" --backend local --max-retries 1
"$CRUN" watch 1 --timeout 60

echo "submitting a failing job: expecting app-error exit 20"
"$CRUN" submit "$TMP/fail.json"
set +e
"$CRUN" watch 2 --timeout 60
CODE=$?
set -e
if [[ "$CODE" -ne 20 ]]; then
  echo "expected exit 20 for an app-error dead-letter, got $CODE" >&2
  exit 1
fi

echo "queue after the run"
"$CRUN" queue

echo "e2e ok"
