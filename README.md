# ClusterRun

A distributed job runner for heavy compute, written in C++20. A controller and a
fleet of workers coordinate over a shared queue. Workers run as local processes
or AWS instances, self-report their capabilities (cpu, memory, AVX2, GPU), and
the controller matches jobs to workers via a declared `requires` block. Workers
stream status back; if a worker is lost mid-run the controller redrives the job,
and long-running jobs checkpoint so they can resume on a different worker.

This is a portfolio project. It is intentionally C++ end to end, with a hybrid
AWS-plus-local worker topology and a heavy-compute focus where jobs run for
seconds to hours and checkpointing is load-bearing.

## Architecture

- **Controller** consumes job descriptors from a queue, tracks worker capability
  heartbeats, matches each job to an eligible worker, and watches for loss.
- **Workers** claim jobs that match their capabilities, fork/exec the command,
  stream stdout/stderr to object storage, and emit status plus heartbeats.
- **Node classes** let a worker declare where it lives and what shape of work it
  is meant for: `aws`, `local`, `gpu`, or `cpu-heavy`. A job sets
  `requires.node_class` to pin itself to a class, and `requires.features` to pin
  specific hardware (for example `gpu`). A job whose class or features no live
  worker satisfies waits in a pending-capability set with a `waiting_for` reason
  rather than running on the wrong worker.
- **Transports** wrap a queue (SQS-shaped) and an object store (S3-shaped). The
  default build talks to LocalStack over its REST API through a small libcurl
  client, which keeps continuous integration hermetic and fast. The same
  interface can be backed by the AWS SDK for C++ for real deployments.

## Job descriptor

```json
{
  "job_id": 42,
  "command": ["./solve", "--n", "1000000"],
  "stdout_s3": "s3://results/42.out",
  "stderr_s3": "s3://results/42.err",
  "requires": { "cpu_min": 4, "mem_gb_min": 8, "features": ["avx2"], "node_class": "cpu-heavy" },
  "attempts_max": 3,
  "checkpoint_interval_s": 10,
  "idempotency_key": "solve-1000000"
}
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Sanitizers

```sh
cmake -S . -B build-asan -DCLUSTERRUN_ASAN=ON && cmake --build build-asan -j
cmake -S . -B build-tsan -DCLUSTERRUN_TSAN=ON && cmake --build build-tsan -j
```

## LocalStack

```sh
docker compose up -d localstack
export AWS_ENDPOINT_URL=http://localhost:4566
```

## License

MIT.
