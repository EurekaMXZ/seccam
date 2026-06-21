#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
HOST_TOOLS_HOME="${SECCAM_HOST_TOOLS_HOME:-${ROOT_DIR}/host_tools}"
HOST_TOOLS_ROOT="${SECCAM_HOST_TOOLS_ROOT:-${HOST_TOOLS_HOME}/toolchain}"
HOST_TOOLS_REPO="${SECCAM_HOST_TOOLS_REPO:-https://github.com/milkv-duo/host-tools.git}"
HOST_TOOLS_REF="${SECCAM_HOST_TOOLS_REF:-67688c7335e7239f7592b86ed1432c97cbab245b}"
SDK_REPO="${SECCAM_SDK_V2_REPO:-https://github.com/milkv-duo/duo-buildroot-sdk-v2.git}"
SDK_REF="${SECCAM_SDK_V2_REF:-v2.0.1}"
SDK_ROOT="${SECCAM_CORE_VENDOR_ROOT:-${HOST_TOOLS_HOME}/sdk/duo-buildroot-sdk-v2-${SDK_REF}}"

mkdir -p "${HOST_TOOLS_HOME}/sdk"

checkout_repo_ref() {
  local repo_url="$1"
  local repo_dir="$2"
  local repo_ref="$3"

  if [[ ! -d "${repo_dir}/.git" ]]; then
    mkdir -p "$(dirname "${repo_dir}")"
    git init "${repo_dir}" >/dev/null
    git -C "${repo_dir}" remote add origin "${repo_url}"
  fi

  git config --global --add safe.directory "${repo_dir}"
  git -C "${repo_dir}" fetch --depth 1 origin "${repo_ref}" >/dev/null
  git -C "${repo_dir}" checkout --force --detach FETCH_HEAD >/dev/null
}

prepare_sparse_sdk_checkout() {
  if [[ ! -d "${SDK_ROOT}/.git" ]]; then
    mkdir -p "$(dirname "${SDK_ROOT}")"
    git init "${SDK_ROOT}" >/dev/null
    git -C "${SDK_ROOT}" remote add origin "${SDK_REPO}"
  fi

  git config --global --add safe.directory "${SDK_ROOT}"
  git -C "${SDK_ROOT}" config core.sparseCheckout true
  git -C "${SDK_ROOT}" sparse-checkout set --no-cone cvi_mpi cvi_rtsp tdl_sdk >/dev/null

  git -C "${SDK_ROOT}" fetch --depth 1 origin "${SDK_REF}" >/dev/null
  git -C "${SDK_ROOT}" checkout --force --detach FETCH_HEAD >/dev/null
}

require_file() {
  local file_path="$1"
  if [[ ! -f "${file_path}" ]]; then
    echo "missing required file: ${file_path}" >&2
    exit 1
  fi
}

require_dir() {
  local dir_path="$1"
  if [[ ! -d "${dir_path}" ]]; then
    echo "missing required directory: ${dir_path}" >&2
    exit 1
  fi
}

checkout_repo_ref "${HOST_TOOLS_REPO}" "${HOST_TOOLS_ROOT}" "${HOST_TOOLS_REF}"
prepare_sparse_sdk_checkout

require_dir "${HOST_TOOLS_ROOT}/gcc/riscv64-linux-musl-x86_64/bin"
require_file "${HOST_TOOLS_ROOT}/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc"
require_file "${HOST_TOOLS_ROOT}/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-strip"
require_dir "${SDK_ROOT}/cvi_mpi/include"
require_dir "${SDK_ROOT}/cvi_rtsp/include"
require_dir "${SDK_ROOT}/tdl_sdk/include"

echo "SECCAM_HOST_TOOLS_ROOT=${HOST_TOOLS_ROOT}"
echo "SECCAM_CORE_VENDOR_ROOT=${SDK_ROOT}"
