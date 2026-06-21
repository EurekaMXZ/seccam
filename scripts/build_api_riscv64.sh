#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TARGET="${SECCAM_API_TARGET:-riscv64gc-unknown-linux-musl}"
HOST_TOOLS_ROOT="${SECCAM_HOST_TOOLS_ROOT:-${ROOT_DIR}/host_tools/toolchain}"
TARGET_DIR="${SECCAM_API_TARGET_DIR:-${ROOT_DIR}/backend/target/${TARGET}/release}"
STRIP_BIN="${HOST_TOOLS_ROOT}/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-strip"

export PATH="${HOME}/.local/bin:${PATH}"

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo is required but not found in PATH" >&2
  exit 1
fi

if ! command -v cargo-zigbuild >/dev/null 2>&1; then
  echo "cargo-zigbuild is required. Install it with: python3 -m pip install cargo-zigbuild" >&2
  exit 1
fi

rustup target add "${TARGET}" >/dev/null

pushd "${ROOT_DIR}/backend" >/dev/null
cargo zigbuild --release --target "${TARGET}" -p seccam-api >&2
popd >/dev/null

API_BIN="${TARGET_DIR}/seccam-api"
if [[ ! -x "${API_BIN}" ]]; then
  echo "missing seccam-api: ${API_BIN}" >&2
  exit 1
fi

if [[ -x "${STRIP_BIN}" ]]; then
  FILE_DESC=$(file -b "${API_BIN}")
  if [[ "${FILE_DESC}" == *"static-pie linked"* ]]; then
    echo "skip strip for static-pie binary: ${API_BIN}" >&2
  else
    "${STRIP_BIN}" --strip-unneeded "${API_BIN}"
  fi
fi

echo "${API_BIN}"
