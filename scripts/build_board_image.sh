#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DOWNLOAD_DIR="${SECCAM_DOWNLOAD_DIR:-${ROOT_DIR}/build/downloads}"
WORK_ROOT="${SECCAM_IMAGE_WORK_ROOT:-${ROOT_DIR}/build/board-image}"
OFFICIAL_URL="${SECCAM_OFFICIAL_IMAGE_URL:-https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases/download/v2.0.1/milkv-duo256m-musl-riscv64-sd_v2.0.1.img.zip}"
OFFICIAL_ZIP="${SECCAM_OFFICIAL_IMAGE_ZIP:-${DOWNLOAD_DIR}/$(basename "${OFFICIAL_URL}")}"
OFFICIAL_IMG_DIR="${WORK_ROOT}/official-src"
OFFICIAL_RUNTIME_ROOT="${ROOT_DIR}/build/runtime/official-v2"
OFFICIAL_IMG=""
mount_dir="${WORK_ROOT}/runtime-mnt"

cleanup() {
  set +e
  if mountpoint -q "${mount_dir}"; then
    umount "${mount_dir}"
  fi
}
trap cleanup EXIT

partition_offset_bytes() {
  local image_path="$1"
  local partition_number="$2"
  local sector_size
  local start_sector

  if command -v sfdisk >/dev/null 2>&1; then
    sector_size=$(sfdisk -d "${image_path}" | awk -F': ' '/^sector-size:/ {gsub(/ /, "", $2); print $2; exit}')
    start_sector=$(sfdisk -d "${image_path}" | awk -v part="${partition_number}" -F'[=, ]+' '
      $1 ~ ("\\.img" part "$") {
        for (i = 1; i <= NF; ++i) {
          if ($i == "start") {
            print $(i + 1)
            exit
          }
        }
      }
    ')
  elif command -v fdisk >/dev/null 2>&1; then
    sector_size=$(fdisk -l "${image_path}" | awk '/^Sector size/ {print $4; exit}')
    start_sector=$(fdisk -l "${image_path}" | awk -v part="${partition_number}" '
      $1 ~ ("\\.img" part "$") {
        print $2
        exit
      }
    ')
  fi

  if [[ -z "${sector_size}" || -z "${start_sector}" ]]; then
    echo "failed to resolve partition offset: ${image_path} partition ${partition_number}" >&2
    exit 1
  fi

  echo $((sector_size * start_sector))
}

download_official_image() {
  mkdir -p "${DOWNLOAD_DIR}"
  if [[ ! -f "${OFFICIAL_ZIP}" ]]; then
    curl -L --fail --output "${OFFICIAL_ZIP}" "${OFFICIAL_URL}"
  fi
}

extract_official_image() {
  mkdir -p "${OFFICIAL_IMG_DIR}"
  rm -f "${OFFICIAL_IMG_DIR}"/*.img
  unzip -o "${OFFICIAL_ZIP}" -d "${OFFICIAL_IMG_DIR}" >/dev/null
  OFFICIAL_IMG=$(find "${OFFICIAL_IMG_DIR}" -maxdepth 1 -type f -name '*.img' | head -n 1)
  if [[ -z "${OFFICIAL_IMG}" ]]; then
    echo "failed to extract official .img from ${OFFICIAL_ZIP}" >&2
    exit 1
  fi
}

extract_runtime_libraries() {
  local rootfs_offset

  rm -rf "${OFFICIAL_RUNTIME_ROOT}"
  mkdir -p "${OFFICIAL_RUNTIME_ROOT}" "${mount_dir}"

  rootfs_offset=$(partition_offset_bytes "${OFFICIAL_IMG}" 2)
  mount -o loop,offset="${rootfs_offset}" "${OFFICIAL_IMG}" "${mount_dir}"

  mkdir -p "${OFFICIAL_RUNTIME_ROOT}/lib" \
    "${OFFICIAL_RUNTIME_ROOT}/usr/lib/3rd"
  rsync -a "${mount_dir}/mnt/system/lib/" "${OFFICIAL_RUNTIME_ROOT}/lib/"
  rsync -a "${mount_dir}/mnt/system/usr/lib/" "${OFFICIAL_RUNTIME_ROOT}/usr/lib/"
  if [[ -d "${mount_dir}/mnt/system/usr/lib/3rd" ]]; then
    rsync -a "${mount_dir}/mnt/system/usr/lib/3rd/" "${OFFICIAL_RUNTIME_ROOT}/usr/lib/3rd/"
  fi

  umount "${mount_dir}"
}

"${ROOT_DIR}/scripts/prepare_duo256m_sdk.sh"
download_official_image
extract_official_image
extract_runtime_libraries

export SECCAM_RUNTIME_ROOT="${OFFICIAL_RUNTIME_ROOT}"

CORE_BIN=$("${ROOT_DIR}/scripts/build_core_riscv64.sh")
API_BIN=$("${ROOT_DIR}/scripts/build_api_riscv64.sh")

SECCAM_OFFICIAL_IMAGE_PATH="${OFFICIAL_ZIP}" \
SECCAM_CORE_BIN="${CORE_BIN}" \
SECCAM_API_BIN="${API_BIN}" \
SECCAM_IMAGE_WORK_ROOT="${WORK_ROOT}" \
"${ROOT_DIR}/scripts/repack_official_img.sh"
