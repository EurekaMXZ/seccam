FROM rust:1.95-bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    dosfstools \
    e2fsprogs \
    fdisk \
    file \
    git \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    rsync \
    unzip \
    util-linux \
    xz-utils \
    zip \
  && rm -rf /var/lib/apt/lists/*

RUN rustup target add riscv64gc-unknown-linux-musl

RUN python3 -m venv /opt/cargo-zigbuild \
  && /opt/cargo-zigbuild/bin/pip install --no-cache-dir --upgrade pip \
  && /opt/cargo-zigbuild/bin/pip install --no-cache-dir cargo-zigbuild

ENV PATH="/opt/cargo-zigbuild/bin:${PATH}"

WORKDIR /workspace
