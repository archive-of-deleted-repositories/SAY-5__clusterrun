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
  "worker_id": 7,
  "detail": "app-error: exit code 3",
  "category": "app-error",
  "checkpoint": "s3://ck/42/10"
}
```

`worker_id`, `detail`, `category`, and `checkpoint` are present only when they
have a value. `state` is one of:

| state | meaning |
|---|---|
| `pending` | queued, not yet assigned |
| `waiting` | no live worker satisfies its `requires` block |
| `claimed` | dispatched to a worker |
| `running` | the worker has started the process |
| `checkpointed` | running, with at least one checkpoint persisted |
| `succeeded` | terminal success |
| `dead_lettered` | terminal failure, no attempts left |

### `GET /v1/queue`

Queue depths.

```json
{
  "pending": 3,
  "in_flight": 2,
  "done": 40,
  "dead_lettered": 1,
  "waiting": {"9": "no worker satisfies feature:gpu"}
}
```

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
| 20 | dead-lettered: `app-error` |
| 21 | dead-lettered: `crash` |
| 22 | dead-lettered: `oom` |
| 23 | dead-lettered: `preempted` |
| 24 | dead-lettered: `deadline` |
| 25 | dead-lettered: `spawn-failure` |
| 26 | dead-lettered: `upload-failure` |
| 27 | dead-lettered: `lost` |
| 29 | dead-lettered: category unknown |
