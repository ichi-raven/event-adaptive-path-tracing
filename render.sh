#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BINARY_DIR="${ROOT_DIR}/build/bin"
MAIN_EXE="${BINARY_DIR}/eapt"

INPUT_SCENE="${1:-${ROOT_DIR}/scenes/cornell_box/scene.json}"
SCENE_NAME="$(basename -- "$(dirname -- "${INPUT_SCENE}")")"
OUTPUT_DIR="${2:-${ROOT_DIR}/outputs/${SCENE_NAME%.*}}"

if (($# >= 1)); then
  shift
fi
if (($# >= 1)); then
  shift
fi

if [[ ! -x "${MAIN_EXE}" ]]; then
  printf 'Renderer executable not found: %s\n' "${MAIN_EXE}" >&2
  printf 'Build it first with: cmake --build build\n' >&2
  exit 1
fi

if [[ "${INPUT_SCENE}" != /* ]]; then
  INPUT_SCENE="${ROOT_DIR}/${INPUT_SCENE}"
fi
if [[ "${OUTPUT_DIR}" != /* ]]; then
  OUTPUT_DIR="${ROOT_DIR}/${OUTPUT_DIR}"
fi

if [[ ! -f "${INPUT_SCENE}" ]]; then
  printf 'Input scene not found: %s\n' "${INPUT_SCENE}" >&2
  exit 1
fi

cd -- "${BINARY_DIR}"
exec "${MAIN_EXE}" -i "${INPUT_SCENE}" -o "${OUTPUT_DIR}" --verbose "$@"
