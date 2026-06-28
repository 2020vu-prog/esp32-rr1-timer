#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <rr1-messages-generated artifact directory>" >&2
  exit 2
fi

artifact_dir="$1"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
component_dir="${repo_root}/components/rr1_messages"

required_files=(
  "timer.pb-c.c"
  "timestamp.pb-c.c"
  "include/timer.pb-c.h"
  "include/google/protobuf/timestamp.pb-c.h"
)

for file in "${required_files[@]}"; do
  if [[ ! -f "${artifact_dir}/${file}" ]]; then
    echo "missing generated artifact file: ${file}" >&2
    exit 1
  fi
done

for file in "${required_files[@]}"; do
  mkdir -p "$(dirname "${component_dir}/${file}")"
  cp "${artifact_dir}/${file}" "${component_dir}/${file}"
done

if command -v clang-format >/dev/null 2>&1; then
  generated_files=()
  for file in "${required_files[@]}"; do
    generated_files+=("${component_dir}/${file}")
  done
  clang-format --style=LLVM -i "${generated_files[@]}"
else
  echo "clang-format not found; generated files were copied without formatting" >&2
fi
