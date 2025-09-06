#!/bin/bash

QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
BUILD_DIR="${QEMU_DIR}/build"
OUTPUT_DIR="${QEMU_DIR}/output"

cd $OUTPUT_DIR
"${BUILD_DIR}/qemu-img" create -f raw root 16G
"${BUILD_DIR}/qemu-img" create -f raw firmware 8M
"${BUILD_DIR}/qemu-img" create -f raw syscfg 128K
"${BUILD_DIR}/qemu-img" create -f raw ctrl_bits 8K
"${BUILD_DIR}/qemu-img" create -f raw nvram 8K
"${BUILD_DIR}/qemu-img" create -f raw effaceable 4K
"${BUILD_DIR}/qemu-img" create -f raw panic_log 1M
"${BUILD_DIR}/qemu-img" create -f raw sep_nvram 2K
"${BUILD_DIR}/qemu-img" create -f raw sep_ssc 128K