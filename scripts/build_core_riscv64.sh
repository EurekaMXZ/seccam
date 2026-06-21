#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${SECCAM_CORE_BUILD_DIR:-${ROOT_DIR}/build/duo-riscv64-release}"
HOST_TOOLS_ROOT="${SECCAM_HOST_TOOLS_ROOT:-${ROOT_DIR}/host_tools/toolchain}"
SDK_ROOT="${SECCAM_CORE_VENDOR_ROOT:-${ROOT_DIR}/host_tools/sdk/duo-buildroot-sdk-v2-v2.0.1}"
RUNTIME_ROOT="${SECCAM_RUNTIME_ROOT:-${ROOT_DIR}/build/runtime/official-v2}"
JOBS="${SECCAM_BUILD_JOBS:-$(nproc)}"
STRIP_BIN="${HOST_TOOLS_ROOT}/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-strip"

export SECCAM_HOST_TOOLS_ROOT="${HOST_TOOLS_ROOT}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/riscv64_musl_duo256m.cmake" \
  -DSECCAM_BUILD_CORE_DAEMON=ON \
  -DSECCAM_CORE_VENDOR_ROOT="${SDK_ROOT}" \
  -DSECCAM_RUNTIME_ROOT="${RUNTIME_ROOT}" \
  -DCMAKE_BUILD_TYPE=Release >&2

cmake --build "${BUILD_DIR}" -j"${JOBS}" >&2

CORE_BIN="${BUILD_DIR}/core/seccam-core"
if [[ ! -x "${CORE_BIN}" ]]; then
  echo "missing seccam-core: ${CORE_BIN}" >&2
  exit 1
fi

if [[ -x "${STRIP_BIN}" ]]; then
  "${STRIP_BIN}" --strip-unneeded "${CORE_BIN}"
fi

echo "${CORE_BIN}"
