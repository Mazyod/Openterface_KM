#!/usr/bin/env bash
set -euo pipefail

# Periodic low-volume top sampling over BLE-SSH proxy.
# Assumes the BLE proxy is already running (for example on 127.0.0.1:18048).

PORT="18048"
USER_NAME="pengtianyu"
PASSWORD="sunny808"
INTERVAL="3"
TOP_N="15"
TOP_STATS="pid,command,cpu,mem"
CONNECT_TIMEOUT="10"

usage() {
  cat <<'EOF'
Usage:
  tools/ble_top_poll.sh [options]

Options:
  --port <port>             Local proxy port (default: 18048)
  --user <name>             SSH username (default: pengtianyu)
  --password <pass>         SSH password for sshpass (default: sunny808)
  --interval <sec>          Loop interval seconds (default: 3)
  --top-n <num>             Number of processes in top snapshot (default: 15)
  --top-stats <fields>      top stats fields (default: pid,command,cpu,mem)
  --connect-timeout <sec>   SSH connect timeout (default: 10)
  -h, --help                Show this help

Example:
  tools/ble_top_poll.sh --interval 3 --top-n 12 --top-stats pid,command,cpu,mem
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    --user)
      USER_NAME="$2"
      shift 2
      ;;
    --password)
      PASSWORD="$2"
      shift 2
      ;;
    --interval)
      INTERVAL="$2"
      shift 2
      ;;
    --top-n)
      TOP_N="$2"
      shift 2
      ;;
    --top-stats)
      TOP_STATS="$2"
      shift 2
      ;;
    --connect-timeout)
      CONNECT_TIMEOUT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if ! command -v sshpass >/dev/null 2>&1; then
  echo "ERROR: sshpass is required. Install it first." >&2
  exit 1
fi

echo "Starting BLE top polling loop: user=${USER_NAME} port=${PORT} interval=${INTERVAL}s top_n=${TOP_N}"
echo "Press Ctrl+C to stop."

while true; do
  echo "===== $(date '+%Y-%m-%d %H:%M:%S') ====="

  if ! sshpass -p "${PASSWORD}" ssh -p "${PORT}" \
      -o ConnectTimeout="${CONNECT_TIMEOUT}" \
      -o NumberOfPasswordPrompts=1 \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o PreferredAuthentications=keyboard-interactive,password \
      -o PubkeyAuthentication=no \
      -o Compression=no \
      -o KexAlgorithms=curve25519-sha256 \
      -o HostKeyAlgorithms=ssh-ed25519,rsa-sha2-512,rsa-sha2-256 \
      -o Ciphers=aes128-ctr \
      -o MACs=hmac-sha2-256 \
      "${USER_NAME}@127.0.0.1" \
      "top -l 1 -n ${TOP_N} -stats ${TOP_STATS}"; then
    echo "WARN: SSH command failed. Retrying after ${INTERVAL}s..." >&2
  fi

  sleep "${INTERVAL}"
done
