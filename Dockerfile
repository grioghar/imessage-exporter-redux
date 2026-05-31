# Multi-stage build for the imessage-exporter CLI.
#
# Usage (mount a directory holding your data; results are written back into it):
#   docker build -t imessage-exporter .
#   docker run --rm -v "$PWD:/data" imessage-exporter \
#       --db /data/chat.db --format html --output /data/export
#
# Or, without mounting, copy files in and the results out:
#   id=$(docker create imessage-exporter --db /data/chat.db --output /data/export)
#   docker cp ./chat.db "$id:/data/chat.db"
#   docker start -a "$id"
#   docker cp "$id:/data/export" ./export
#   docker rm "$id"

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ make libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/imessage-exporter /usr/local/bin/imessage-exporter
# Conventional mount point for your data; bind-mount a host directory here.
WORKDIR /data
ENTRYPOINT ["imessage-exporter"]
CMD ["--help"]
