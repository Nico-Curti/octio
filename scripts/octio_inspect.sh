#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: scripts/octio_inspect.sh <file.fda|file.e2e> [--json]" >&2
}

if (($# < 1 || $# > 2)); then
  usage
  exit 2
fi

FILE="$1"
JSON_FLAG=""
if (($# == 2)); then
  if [[ "$2" != "--json" ]]; then
    usage
    exit 2
  fi
  JSON_FLAG="--json"
fi

if command -v octio >/dev/null 2>&1; then
  if [[ -n "$JSON_FLAG" ]]; then
    exec octio inspect "$FILE" "$JSON_FLAG"
  fi
  exec octio inspect "$FILE"
fi

if [[ -n "$JSON_FLAG" ]]; then
  exec python -m octio.cli inspect "$FILE" "$JSON_FLAG"
fi
exec python -m octio.cli inspect "$FILE"
