#!/bin/sh

set -eu

SECCAM_ROOT=/mnt/data/seccam
SECCAM_BIN_DIR=${SECCAM_ROOT}/bin
SECCAM_LOG_DIR=${SECCAM_ROOT}/logs
SECCAM_ENV_FILE=${SECCAM_ROOT}/seccam-api.env
SECCAM_CORE_BIN=${SECCAM_BIN_DIR}/seccam-core
SECCAM_API_BIN=${SECCAM_BIN_DIR}/seccam-api
SECCAM_CORE_CONFIG=${SECCAM_ROOT}/seccam-core.ini
SECCAM_CORE_SOCKET=/var/run/seccam-core.sock
SECCAM_CORE_LOG=${SECCAM_LOG_DIR}/seccam-core.log
SECCAM_API_LOG=${SECCAM_LOG_DIR}/seccam-api.log

load_api_env() {
  if [ -f "${SECCAM_ENV_FILE}" ]; then
    # shellcheck disable=SC1090
    . "${SECCAM_ENV_FILE}"
  fi

  : "${SECCAM_BACKEND_ADDR:=0.0.0.0:8080}"
  : "${SECCAM_CORE_SOCKET:=/var/run/seccam-core.sock}"
  : "${SECCAM_STORE_PATH:=/mnt/data/seccam/seccam-backend.sqlite3}"
  : "${SECCAM_RTSP_HOST:=192.168.42.1}"
  : "${SECCAM_DEVICE_ID:=duo-256m-001}"
  : "${SECCAM_DEVICE_NAME:=MilkV Duo 256M}"
  : "${RUST_LOG:=info}"

  export SECCAM_BACKEND_ADDR
  export SECCAM_CORE_SOCKET
  export SECCAM_STORE_PATH
  export SECCAM_RTSP_HOST
  export SECCAM_DEVICE_ID
  export SECCAM_DEVICE_NAME
  export RUST_LOG
}

ensure_runtime_dirs() {
  mkdir -p "${SECCAM_BIN_DIR}" "${SECCAM_ROOT}/recordings" "${SECCAM_LOG_DIR}"
}

start_core() {
  if pidof seccam-core >/dev/null 2>&1; then
    return 0
  fi

  rm -f "${SECCAM_CORE_SOCKET}"
  "${SECCAM_CORE_BIN}" --config "${SECCAM_CORE_CONFIG}" >>"${SECCAM_CORE_LOG}" 2>&1 &
}

wait_for_core_socket() {
  i=0
  while [ "${i}" -lt 100 ]; do
    if [ -S "${SECCAM_CORE_SOCKET}" ]; then
      return 0
    fi
    sleep 0.1
    i=$((i + 1))
  done

  echo "seccam-core socket is not ready: ${SECCAM_CORE_SOCKET}" >&2
  return 1
}

start_api() {
  if pidof seccam-api >/dev/null 2>&1; then
    return 0
  fi

  load_api_env
  "${SECCAM_API_BIN}" >>"${SECCAM_API_LOG}" 2>&1 &
}

ensure_runtime_dirs
start_core
wait_for_core_socket
start_api
