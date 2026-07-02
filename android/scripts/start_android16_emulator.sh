#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "${script_dir}/android_ssd_env.sh"

usage() {
    cat <<EOF
usage: ./scripts/start_android16_emulator.sh [--foreground] [-- emulator args]

Starts the Android 16 AVD stored on the SSD.
  Default mode: launches in background, waits for boot, logs to the AVD directory.
  --foreground: runs the emulator in the current terminal.
EOF
}

require_path() {
    path_value=$1
    description=$2
    if [ ! -e "${path_value}" ]; then
        echo "${description} not found: ${path_value}" >&2
        exit 1
    fi
}

pick_emulator_serial() {
    "${LATRY_ADB_BIN}" devices | awk '$2 == "device" && $1 ~ /^emulator-/ { print $1; exit }'
}

foreground=0

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

if [ "${1:-}" = "--foreground" ]; then
    foreground=1
    shift
fi

if [ "${1:-}" = "--" ]; then
    shift
fi

require_path "${LATRY_EMULATOR_BIN}" "emulator"
require_path "${LATRY_ADB_BIN}" "adb"
require_path "${LATRY_ANDROID_AVD_HOME}/${LATRY_ANDROID_AVD_NAME}.avd" "Android 16 AVD"

avd_dir="${LATRY_ANDROID_AVD_HOME}/${LATRY_ANDROID_AVD_NAME}.avd"
log_file="${avd_dir}/emulator.log"

if [ "${foreground}" -eq 1 ]; then
    exec env \
        ANDROID_HOME="${LATRY_ANDROID_SDK_ROOT}" \
        ANDROID_SDK_ROOT="${LATRY_ANDROID_SDK_ROOT}" \
        ANDROID_USER_HOME="${LATRY_ANDROID_USER_HOME}" \
        ANDROID_AVD_HOME="${LATRY_ANDROID_AVD_HOME}" \
        "${LATRY_EMULATOR_BIN}" \
            -avd "${LATRY_ANDROID_AVD_NAME}" \
            -datadir "${avd_dir}" \
            -gpu auto \
            -cores 4 \
            -memory 3072 \
            -netfast \
            -allow-host-audio \
            "$@"
fi

nohup env \
    ANDROID_HOME="${LATRY_ANDROID_SDK_ROOT}" \
    ANDROID_SDK_ROOT="${LATRY_ANDROID_SDK_ROOT}" \
    ANDROID_USER_HOME="${LATRY_ANDROID_USER_HOME}" \
    ANDROID_AVD_HOME="${LATRY_ANDROID_AVD_HOME}" \
    "${LATRY_EMULATOR_BIN}" \
        -avd "${LATRY_ANDROID_AVD_NAME}" \
        -datadir "${avd_dir}" \
        -gpu auto \
        -cores 4 \
        -memory 3072 \
        -netfast \
        -allow-host-audio \
        "$@" >"${log_file}" 2>&1 &

emulator_pid=$!
echo "Started Android 16 emulator with PID ${emulator_pid}"
echo "Log file: ${log_file}"

boot_completed=""
attempt=0
while [ "${attempt}" -lt 180 ]; do
    serial=$(pick_emulator_serial || true)
    if [ -n "${serial}" ]; then
        boot_completed=$("${LATRY_ADB_BIN}" -s "${serial}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
        if [ "${boot_completed}" = "1" ]; then
            echo "Emulator boot completed on ${serial}"
            exit 0
        fi
    fi
    attempt=$((attempt + 1))
    sleep 2
done

echo "Emulator started, but Android did not report boot completion within 6 minutes." >&2
exit 1
