# Control protocol

The controller exposes a small HTTP/1.1 control endpoint that mirrors its
internal submit/status API. It exists so external clients (the `crun` CLI in
`cli/`) can drive the system without speaking the SQS transport directly. All
bodies are JSON. The port defaults to 8447 and is set with
`CLUSTERRUN_CONTROL_PORT`.

## Endpoints

### `GET /v1/healthz`

Liveness probe.

```json
{"status": "ok", "version": "2.0.0"}
```

### `POST /v1/jobs`

Body is a job descriptor in the same JSON schema the jobs queue accepts (see
README). The job is validated with the same codec the queue intake uses;
malformed descriptors get a 400. A `job_id` the controller already knows is
rejected with a 400 rather than silently replacing state.

```json
{"job_id": 42, "state": "pending"}
```

### `GET /v1/jobs/{id}`

Live state of a job. 404 for an unknown id.

```json
{
  "job_id": 42,
  "state": "running",
  "attempts": 1,
  "attempts_max": 3,
  "priority": "high",
  "tenant": "acme",
  "worker_id": 7,
  "detail": "app-error: exit code 3",
  "category": "app-error",
  "checkpoint": "s3://ck/42/10"
}
```

`worker_id`, `tenant`, `detail`, `category`, and `checkpoint` are present only
when they have a value. Once a result artifact has been uploaded, `artifact`
(its URI) and `artifact_sha256` appear as well. `state` is one of:

| state | meaning |
|---|---|
| `pending` | queued, not yet assigned |
| `waiting` | no live worker satisfies its `requires` block |
| `claimed` | dispatched to a worker |
| `running` | the worker has started the process |
| `checkpointed` | running, with at least one checkpoint persisted |
| `succeeded` | terminal success |
| `dead_lettered` | terminal failure, no attempts left |

### `GET /v1/jobs/{id}/result`

Downloads the job's result artifact through the controller. The body is the
raw artifact bytes (`application/octet-stream`) and the `X-Checksum-Sha256`
header carries the checksum the worker computed at upload time, so a client
can verify the download end to end. 404 if the job is unknown or has no
artifact yet.

### `GET /v1/queue`

Queue depths.

```json
{
  "pending": 3,
  "pending_by_priority": {"high": 1, "normal": 2, "low": 0},
  "in_flight": 2,
  "done": 40,
  "dead_lettered": 1,
  "waiting": {"9": "no worker satisfies feature:gpu"}
}
```

### `GET /v1/stats`

Aggregated run counters for `crun stats`.

```json
{
  "submitted": 10,
  "done": 7,
  "dead_lettered": 1,
  "pending": 2,
  "in_flight": 0,
  "retries_by_category": {"preempted": 2, "lost": 1},
  "latency_p50_us": 250000,
  "latency_p95_us": 1200000,
  "throughput_per_s": 3.5
}
```

Latency is claim-to-success on the worker's clock, percentiles by nearest
rank. Throughput is completions divided by the span from the first claim to
the most recent success.

### `POST /v1/workers/{id}/drain`

Marks a worker as draining: it finishes whatever it is currently running but
the scheduler assigns it nothing new. Jobs that only this worker could satisfy
wait with a `waiting_for` reason. Draining survives subsequent heartbeats and
clears if the worker is reaped and re-registers. 404 for a worker that has
never sent a heartbeat.

```json
{"worker_id": 7, "draining": true}
```

## Priorities and fair queues

A job descriptor may carry `"priority"` (`high`, `normal`, or `low`; the
default is `normal`) and `"tenant"` (any string; empty means the default
tenant). The scheduler drains the high band completely before normal, and
normal before low. Within one band, tenants take turns in round-robin order so
one tenant's burst cannot starve another. `crun submit --priority high` sets
the field from the command line.

## Result artifacts

A job descriptor may set `"result_s3"` to an object URI. The worker then hands
the child process a scratch path in `CLUSTERRUN_RESULT_FILE`; on a successful
exit it uploads that file to the destination and reports its sha256 to the
controller. A missing or unuploadable result file fails the attempt with the
`upload-failure` category. `crun fetch <job-id>` downloads the artifact and
refuses to write it if the checksum does not match.

Artifacts live in the object store selected by `CLUSTERRUN_BACKEND` on the
controller and workers: the S3-shaped store (LocalStack in development) by
default, or a shared filesystem tree rooted at `CLUSTERRUN_FS_ROOT` when set
to `local`.

## Error categories

Failure details carry a leading category token (`<category>: <message>`),
produced by the worker and bucketed by the same policy as the retry
classifier:

| category | source | retried |
|---|---|---|
| `app-error` | clean non-zero exit | no |
| `crash` | crashing signal (SIGSEGV, SIGABRT, ...) | no |
| `oom` | kernel OOM kill | no |
| `preempted` | SIGKILL/SIGTERM | yes |
| `deadline` | controller deadline elapsed | yes |
| `spawn-failure` | fork/exec failed on the worker | no |
| `upload-failure` | output upload to the object store failed | no |
| `lost` | worker went silent mid-run | yes |

## crun exit codes

`crun watch` (and `crun submit --wait`) exits with a code derived from the
job's terminal state so scripts can branch on the failure category:

| exit | meaning |
|---|---|
| 0 | job succeeded |
| 2 | usage error (bad flags, unreadable jobspec) |
| 3 | controller unreachable or transport error |
| 4 | watch timeout elapsed before the job finished |
| 5 | fetched artifact failed checksum verification |
| 20 | dead-lettered: `app-error` |
| 21 | dead-lettered: `crash` |
| 22 | dead-lettered: `oom` |
| 23 | dead-lettered: `preempted` |
| 24 | dead-lettered: `deadline` |
| 25 | dead-lettered: `spawn-failure` |
| 26 | dead-lettered: `upload-failure` |
| 27 | dead-lettered: `lost` |
| 29 | dead-lettered: category unknown |
