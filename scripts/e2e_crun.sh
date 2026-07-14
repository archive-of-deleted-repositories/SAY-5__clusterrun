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
printf '{"job_id":1,"command":["sh","-c","echo crun e2e; printf e2e-artifact-payload > \\"$CLUSTERRUN_RESULT_FILE\\""],"stdout_s3":"s3://results/e2e-1.out","result_s3":"s3://results/e2e-1.bin","attempts_max":2}' \
  > "$TMP/ok.json"
printf '{"job_id":2,"command":["sh","-c","exit 3"],"attempts_max":1}' \
  > "$TMP/fail.json"

echo "submitting and watching a passing job"
"$CRUN" submit "$TMP/ok.json" --backend local --max-retries 1
"$CRUN" watch 1 --timeout 60

echo "fetching and verifying the result artifact"
"$CRUN" fetch 1 --output "$TMP/artifact.bin"
if [[ "$(cat "$TMP/artifact.bin")" != "e2e-artifact-payload" ]]; then
  echo "artifact content mismatch" >&2
  exit 1
fi

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

echo "stats after the run"
"$CRUN" stats
DONE=$(curl -sf "$CRUN_CONTROLLER/v1/stats" | grep -o '"done":[0-9]*' | cut -d: -f2)
if [[ "$DONE" -lt 1 ]]; then
  echo "stats disagree with the run: done=$DONE" >&2
  exit 1
fi

echo "draining worker 1: a new job must stay unassigned"
"$CRUN" drain-worker 1
printf '{"job_id":5,"command":["sh","-c","echo never runs"],"attempts_max":1}' > "$TMP/drained.json"
"$CRUN" submit "$TMP/drained.json"
sleep 5
STATE=$(curl -sf "$CRUN_CONTROLLER/v1/jobs/5" | grep -o '"state":"[a-z_]*"')
if [[ "$STATE" == '"state":"succeeded"' || "$STATE" == '"state":"running"' || "$STATE" == '"state":"claimed"' ]]; then
  echo "drained worker still took new work: $STATE" >&2
  exit 1
fi
echo "job 5 held back as expected: $STATE"

echo "restarting on the local filesystem backend"
kill "$CTRL_PID" "$WORKER_PID" 2>/dev/null || true
wait "$CTRL_PID" "$WORKER_PID" 2>/dev/null || true
export CLUSTERRUN_BACKEND=local
export CLUSTERRUN_FS_ROOT="$TMP/objects"
PORT=$((PORT + 1))
export CLUSTERRUN_CONTROL_PORT="$PORT"
export CRUN_CONTROLLER="http://127.0.0.1:$PORT"
"$CTRL" &
CTRL_PID=$!
CLUSTERRUN_WORKER_ID=1 CLUSTERRUN_CPU=8 CLUSTERRUN_MEM_GB=16 CLUSTERRUN_NODE_CLASS=local \
  "$WORKER" &
WORKER_PID=$!
for _ in $(seq 1 30); do
  if curl -sf "$CRUN_CONTROLLER/v1/healthz" >/dev/null; then break; fi
  sleep 1
done

printf '{"job_id":11,"command":["sh","-c","printf fs-artifact-payload > \\"$CLUSTERRUN_RESULT_FILE\\""],"result_s3":"s3://results/e2e-11.bin","attempts_max":2}' \
  > "$TMP/fs.json"
"$CRUN" submit "$TMP/fs.json"
"$CRUN" watch 11 --timeout 60
"$CRUN" fetch 11 --output "$TMP/artifact-fs.bin"
if [[ "$(cat "$TMP/artifact-fs.bin")" != "fs-artifact-payload" ]]; then
  echo "filesystem-backend artifact mismatch" >&2
  exit 1
fi

echo "e2e ok"
