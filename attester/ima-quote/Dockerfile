FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends tpm2-tools swtpm swtpm-tools python3 ca-certificates && rm -rf /var/lib/apt/lists/*
