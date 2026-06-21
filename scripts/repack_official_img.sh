#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BOARD_ID="milkv-duo256m-musl-riscv64-sd"
PACKAGE_ROOT="${ROOT_DIR}/packaging/${BOARD_ID}"
WORK_ROOT="${SECCAM_IMAGE_WORK_ROOT:-${ROOT_DIR}/build/board-image}"
DIST_DIR="${SECCAM_IMAGE_DIST_DIR:-${ROOT_DIR}/out}"
OFFICIAL_INPUT="${SECCAM_OFFICIAL_IMAGE_PATH:?SECCAM_OFFICIAL_IMAGE_PATH is required}"
CORE_BIN="${SECCAM_CORE_BIN:-${ROOT_DIR}/build/duo-riscv64-release/core/seccam-core}"
API_BIN="${SECCAM_API_BIN:-${ROOT_DIR}/backend/target/riscv64gc-unknown-linux-musl/release/seccam-api}"
OFFICIAL_TAG="${SECCAM_OFFICIAL_IMAGE_TAG:-v2.0.1}"
OUTPUT_BASENAME="${SECCAM_OUTPUT_IMAGE_BASENAME:-seccam-${BOARD_ID}-${OFFICIAL_TAG}}"
MODEL_SOURCE="${SECCAM_MODEL_SOURCE:-}"
MODEL_NAME="${SECCAM_MODEL_NAME:-}"
MODEL_INSTALL_NAME="${SECCAM_MODEL_INSTALL_NAME:-}"
MODEL_THRESHOLD="${SECCAM_MODEL_THRESHOLD:-}"
MODEL_PERSON_CLASS_ID="${SECCAM_MODEL_PERSON_CLASS_ID:-}"

IMG_ROOT="${WORK_ROOT}/img"
STAGE_ROOT="${WORK_ROOT}/stage"
MOUNT_DIR="${WORK_ROOT}/mnt"
OFFICIAL_DIR="${WORK_ROOT}/official"
OUTPUT_IMG="${DIST_DIR}/${OUTPUT_BASENAME}.img"
OUTPUT_ZIP="${DIST_DIR}/${OUTPUT_BASENAME}.img.zip"
OUTPUT_SHA="${DIST_DIR}/${OUTPUT_BASENAME}.img.zip.sha256"

cleanup() {
  set +e
  if mountpoint -q "${MOUNT_DIR}"; then
    umount "${MOUNT_DIR}"
  fi
}
trap cleanup EXIT

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "missing file: ${path}" >&2
    exit 1
  fi
}

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

resolve_model_source() {
  if [[ -n "${MODEL_SOURCE}" ]]; then
    require_file "${MODEL_SOURCE}"
    return
  fi

  if [[ -f "${ROOT_DIR}/assets/models/mobiledetv2-pedestrian-d0-ls-448.cvimodel" ]]; then
    MODEL_SOURCE="${ROOT_DIR}/assets/models/mobiledetv2-pedestrian-d0-ls-448.cvimodel"
    return
  fi

  if [[ -f "${ROOT_DIR}/assets/models/yolov3.cvimodel" ]]; then
    MODEL_SOURCE="${ROOT_DIR}/assets/models/yolov3.cvimodel"
    return
  fi

  echo "SECCAM_MODEL_SOURCE is not set and no supported default model exists under assets/models" >&2
  exit 1
}

resolve_model_metadata() {
  local basename
  basename=$(basename "${MODEL_SOURCE}")

  if [[ -z "${MODEL_NAME}" ]]; then
    case "${basename}" in
      mobiledetv2-pedestrian-d0-ls-448.cvimodel)
        MODEL_NAME="mobiledetv2-pedestrian"
        ;;
      yolov3.cvimodel)
        MODEL_NAME="yolov3"
        ;;
      *)
        echo "SECCAM_MODEL_NAME is required for model file: ${basename}" >&2
        exit 1
        ;;
    esac
  fi

  if [[ -z "${MODEL_INSTALL_NAME}" ]]; then
    MODEL_INSTALL_NAME="${basename}"
  fi

  if [[ -z "${MODEL_THRESHOLD}" ]]; then
    case "${MODEL_NAME}" in
      mobiledetv2-pedestrian)
        MODEL_THRESHOLD="0.02"
        ;;
      *)
        MODEL_THRESHOLD="0.5"
        ;;
    esac
  fi

  if [[ -z "${MODEL_PERSON_CLASS_ID}" ]]; then
    case "${MODEL_NAME}" in
      yolov8-person-pets)
        MODEL_PERSON_CLASS_ID="2"
        ;;
      *)
        MODEL_PERSON_CLASS_ID="0"
        ;;
    esac
  fi
}

prepare_official_img() {
  mkdir -p "${OFFICIAL_DIR}" "${DIST_DIR}"
  rm -f "${OFFICIAL_DIR}"/*.img

  if [[ "${OFFICIAL_INPUT}" == *.zip ]]; then
    unzip -o "${OFFICIAL_INPUT}" -d "${OFFICIAL_DIR}" >/dev/null
  else
    cp -f "${OFFICIAL_INPUT}" "${OFFICIAL_DIR}/"
  fi

  local official_img
  official_img=$(find "${OFFICIAL_DIR}" -maxdepth 1 -type f -name '*.img' | head -n 1)
  if [[ -z "${official_img}" ]]; then
    echo "no .img file found in official image input: ${OFFICIAL_INPUT}" >&2
    exit 1
  fi

  cp -f "${official_img}" "${OUTPUT_IMG}"
}

render_stage_tree() {
  local model_target="/mnt/cvimodel/${MODEL_INSTALL_NAME}"

  rm -rf "${STAGE_ROOT}"
  mkdir -p "${STAGE_ROOT}"
  cp -a "${PACKAGE_ROOT}/." "${STAGE_ROOT}/"

  mkdir -p "${STAGE_ROOT}/mnt/data/seccam/bin" \
    "${STAGE_ROOT}/mnt/cvimodel" \
    "${STAGE_ROOT}/mnt/data/seccam"

  install -m 0755 "${CORE_BIN}" "${STAGE_ROOT}/mnt/data/seccam/bin/seccam-core"
  install -m 0755 "${API_BIN}" "${STAGE_ROOT}/mnt/data/seccam/bin/seccam-api"
  install -m 0644 "${MODEL_SOURCE}" "${STAGE_ROOT}/mnt/cvimodel/${MODEL_INSTALL_NAME}"

  sed \
    -e "s|@MODEL_NAME@|${MODEL_NAME}|g" \
    -e "s|@MODEL_PATH@|${model_target}|g" \
    -e "s|@THRESHOLD@|${MODEL_THRESHOLD}|g" \
    -e "s|@PERSON_CLASS_ID@|${MODEL_PERSON_CLASS_ID}|g" \
    "${PACKAGE_ROOT}/mnt/data/seccam/seccam-core.ini.in" \
    > "${STAGE_ROOT}/mnt/data/seccam/seccam-core.ini"

  chmod 0755 "${STAGE_ROOT}/mnt/system/auto.sh"
  chmod 0755 "${STAGE_ROOT}/mnt/data/seccam/bin/start-seccam.sh"
  rm -f "${STAGE_ROOT}/mnt/data/seccam/seccam-core.ini.in"
}

inject_stage_tree() {
  local rootfs_offset

  mkdir -p "${MOUNT_DIR}"
  rootfs_offset=$(partition_offset_bytes "${OUTPUT_IMG}" 2)
  mount -o loop,offset="${rootfs_offset}" "${OUTPUT_IMG}" "${MOUNT_DIR}"
  cp -a "${STAGE_ROOT}/." "${MOUNT_DIR}/"
  sync
  umount "${MOUNT_DIR}"
}

package_outputs() {
  rm -f "${OUTPUT_ZIP}" "${OUTPUT_SHA}"
  zip -j "${OUTPUT_ZIP}" "${OUTPUT_IMG}" >/dev/null
  sha256sum "${OUTPUT_ZIP}" > "${OUTPUT_SHA}"
}

require_file "${CORE_BIN}"
require_file "${API_BIN}"
resolve_model_source
resolve_model_metadata
prepare_official_img
render_stage_tree
inject_stage_tree
package_outputs

echo "${OUTPUT_ZIP}"
