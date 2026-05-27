# Multi-stage build: a builder image with the C++ toolchain, then a slim runtime.
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git \
      libcurl4-openssl-dev nlohmann-json3-dev \
      libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLUSTERRUN_BUILD_TESTS=OFF -DCLUSTERRUN_BUILD_BENCH=OFF \
    && cmake --build build -j"$(nproc)" --target clusterrun_controller clusterrun_worker

FROM ubuntu:24.04 AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/src/controller/clusterrun_controller /usr/local/bin/
COPY --from=builder /src/build/src/worker/clusterrun_worker /usr/local/bin/

ENTRYPOINT ["/usr/local/bin/clusterrun_controller"]
