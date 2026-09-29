#!/usr/bin/env bash

set -euo pipefail

MY_HOME=/home/tatsuya

# NGFX="$MY_HOME/nvidia/NVIDIA-Nsight-Graphics-2026.3/host/linux-desktop-nomad-x64/ngfx"
NGFX="$MY_HOME/nvidia/NVIDIA-Nsight-Graphics-2025.4/host/linux-desktop-nomad-x64/ngfx"

EA_REPO="$MY_HOME/Documents/Programs/EventAdaptivePT"
EA_ARGS="-i ${EA_REPO}/scenes/fireplace/scene.json -o ${EA_REPO}/outputs/nsight_run01 -w 256 -h 256 --spp 512 -f 2 --mode events --verbose --progress off"

mkdir -p "${EA_REPO}/profiles/ngfx_run01"

exec "${NGFX}" \
  --activity="GPU Trace Profiler" \
  --exe="${EA_REPO}/build/bin/eapt" \
  --dir="${EA_REPO}/build/bin" \
  --args="${EA_ARGS}" \
  --metric-set-id=1 \
  --start-after-ms=0 \
  --max-duration-ms=1000 \
  --architecture=Ada \
  --set-gpu-clocks=unaltered \
  --output-dir="${EA_REPO}/profiles/ngfx_run01" \
  --verbose
