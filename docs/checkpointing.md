# Checkpointing and resume

ClusterRun targets jobs that run for seconds to hours. When a job runs that long,
losing a worker partway through and restarting from zero is expensive. ClusterRun
lets a job checkpoint its progress and resume on a different worker, so a loss at
the 50-minute mark of a one-hour job costs minutes, not the whole hour.

## Checkpoint blob

A checkpoint is a small JSON blob written to object storage:

```json
{ "step": 10, "state": "...opaque job state...", "ts_us": 1717000000000000 }
```

- `step` is the unit of progress the job has completed. On resume the job picks
  up at this step.
- `state` is opaque to ClusterRun. A solver might store its current iterate; a
  renderer might store the last completed frame range.
- `ts_us` records when the checkpoint was taken.

Each job has one checkpoint key, `checkpoints/<job_id>.ckpt`, within the job's
checkpoint bucket. A new checkpoint overwrites the previous one, so resume always
reads the most recent state.

## How a run checkpoints

A worker runs the job step by step and writes a checkpoint every
`checkpoint_interval_s`. After writing the blob to S3 it emits a `checkpointed`
status whose detail is the checkpoint's `s3://` uri:

```
[status] job=42 status=checkpointed progress=33 detail=s3://ckpt/checkpoints/42.ckpt
```

The controller records the latest checkpoint uri it has seen for each job.

## How resume works after a loss

1. A worker is lost mid-run. After 30 seconds of silence the controller declares
   it lost and requeues the job for redrive.
2. On the next assignment pass the controller dispatches the job to a different
   eligible worker, setting `resume_from_checkpoint_s3` to the latest recorded
   checkpoint uri.
3. The resuming worker downloads that checkpoint, restores `step` and `state`,
   and continues from there. Work before the last checkpoint is not repeated.

A cooperating external command receives the checkpoint location through the
`CLUSTERRUN_RESUME_FROM` environment variable; it is responsible for loading the
state and skipping already-completed steps. The in-tree `StepJob` model exercises
this same path deterministically for testing.

## Cost of a mid-run loss

For a 30-second job that checkpoints every 10 seconds and loses its worker at 15
seconds:

- The last durable checkpoint is at the 10-second mark.
- The resuming worker repeats only the 5 seconds between the checkpoint and the
  loss, then runs the remaining 20 seconds: about 35 seconds of total compute.
- A naive full restart would cost the original 30 seconds of lost work plus a
  fresh 30-second run: about 60 seconds.

The regression test `test_checkpoint` pins this behavior: it confirms the resume
starts from the 10-second checkpoint, that total work stays under 40 seconds, and
that the resumed run reaches exactly the state a clean single run would produce.
