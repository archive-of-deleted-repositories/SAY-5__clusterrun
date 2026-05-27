#!/usr/bin/env bash
# End-to-end smoke: start the controller and a worker as local processes against
# LocalStack, submit a handful of jobs, and confirm they complete. Used by CI and
# for local verification.
set -euo pipefail

ENDPOINT="${AWS_ENDPOINT_URL:-http://localhost:4566}"
export AWS_ENDPOINT_URL="$ENDPOINT"
BUILD="${BUILD_DIR:-build}"
CTRL="$BUILD/src/controller/clusterrun_controller"
WORKER="$BUILD/src/worker/clusterrun_worker"
N="${JOBS:-5}"

aws() { command aws --endpoint-url="$ENDPOINT" --region us-east-1 "$@"; }

cleanup() {
  [[ -n "${CTRL_PID:-}" ]] && kill "$CTRL_PID" 2>/dev/null || true
  [[ -n "${WORKER_PID:-}" ]] && kill "$WORKER_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "creating queues"
JOBS_URL=$(aws sqs create-queue --queue-name clusterrun-jobs --query QueueUrl --output text)
DLQ_URL=$(aws sqs create-queue --queue-name clusterrun-dlq --query QueueUrl --output text)

echo "starting controller and worker"
"$CTRL" &
CTRL_PID=$!
CLUSTERRUN_WORKER_ID=1 CLUSTERRUN_CPU=8 CLUSTERRUN_MEM_GB=16 CLUSTERRUN_NODE_CLASS=local \
  "$WORKER" &
WORKER_PID=$!

sleep 2

echo "submitting $N jobs"
for i in $(seq 1 "$N"); do
  BODY=$(printf '{"job_id":%d,"command":["sh","-c","echo job %d"],"stdout_s3":"s3://results/%d.out","attempts_max":2}' "$i" "$i" "$i")
  aws sqs send-message --queue-url "$JOBS_URL" --message-body "$BODY" >/dev/null
done

echo "waiting for completion"
DEADLINE=$((SECONDS + 60))
COMPLETED=0
while (( SECONDS < DEADLINE )); do
  COMPLETED=$(aws s3 ls s3://results/ 2>/dev/null | wc -l | tr -d ' ' || echo 0)
  if (( COMPLETED >= N )); then
    echo "all $N jobs produced output"
    exit 0
  fi
  sleep 2
done

echo "timed out: only $COMPLETED of $N completed" >&2
exit 1
