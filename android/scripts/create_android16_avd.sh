#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
. "${script_dir}/android_ssd_env.sh"

usage() {
    cat <<EOF
usage: ./scripts/create_android16_avd.sh

Creates or normalizes the Android 16 emulator AVD on the SSD-backed Android home:
  AVD name: ${LATRY_ANDROID_AVD_NAME}
  AVD home: ${LATRY_ANDROID_AVD_HOME}
  System image: ${LATRY_ANDROID_SYSTEM_IMAGE}
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

normalize_config() {
    avd_dir="${LATRY_ANDROID_AVD_HOME}/${LATRY_ANDROID_AVD_NAME}.avd"
    config_file="${avd_dir}/config.ini"
    tmp_file="${config_file}.tmp"
    data_file="${avd_dir}/userdata-qemu.img"

    if [ ! -f "${config_file}" ]; then
        return 0
    fi

    awk \
        -v avd_name="${LATRY_ANDROID_AVD_NAME}" \
        -v data_file="${data_file}" \
        '
        BEGIN {
            overrides["avd.id"] = avd_name
            overrides["avd.name"] = avd_name
            overrides["disk.dataPartition.path"] = data_file
            overrides["hw.gpu.enabled"] = "yes"
            overrides["hw.keyboard"] = "yes"
            overrides["showDeviceFrame"] = "no"
        }
        {
            split($0, parts, "=")
            key = parts[1]
            if (key in overrides) {
                print key "=" overrides[key]
                seen[key] = 1
                next
            }
            print
        }
        END {
            for (key in overrides) {
                if (!(key in seen)) {
                    print key "=" overrides[key]
                }
            }
        }
        ' "${config_file}" > "${tmp_file}"

    mv "${tmp_file}" "${config_file}"
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

if [ $# -ne 0 ]; then
    usage >&2
    exit 2
fi

require_path "${LATRY_SDKMANAGER}" "sdkmanager"
require_path "${LATRY_AVDMANAGER}" "avdmanager"

mkdir -p "${LATRY_ANDROID_USER_HOME}" "${LATRY_ANDROID_AVD_HOME}"

if ! "${LATRY_SDKMANAGER}" --sdk_root="${LATRY_ANDROID_SDK_ROOT}" --list_installed \
    | grep -F "${LATRY_ANDROID_SYSTEM_IMAGE}" >/dev/null 2>&1; then
    echo "Missing Android 16 system image: ${LATRY_ANDROID_SYSTEM_IMAGE}" >&2
    echo "Install it with:" >&2
    echo "  ${LATRY_SDKMANAGER} --sdk_root=${LATRY_ANDROID_SDK_ROOT} --install '${LATRY_ANDROID_SYSTEM_IMAGE}'" >&2
    exit 1
fi

if env ANDROID_USER_HOME="${LATRY_ANDROID_USER_HOME}" ANDROID_AVD_HOME="${LATRY_ANDROID_AVD_HOME}" \
    "${LATRY_AVDMANAGER}" list avd | grep -F "Name: ${LATRY_ANDROID_AVD_NAME}" >/dev/null 2>&1; then
    echo "AVD already exists: ${LATRY_ANDROID_AVD_NAME}"
else
    printf 'no\n' | env ANDROID_USER_HOME="${LATRY_ANDROID_USER_HOME}" ANDROID_AVD_HOME="${LATRY_ANDROID_AVD_HOME}" \
        "${LATRY_AVDMANAGER}" create avd \
            --force \
            --name "${LATRY_ANDROID_AVD_NAME}" \
            --package "${LATRY_ANDROID_SYSTEM_IMAGE}" \
            --device "${LATRY_ANDROID_DEVICE_PROFILE}"
    echo "Created AVD: ${LATRY_ANDROID_AVD_NAME}"
fi

normalize_config

echo "AVD ready on SSD:"
echo "  ${LATRY_ANDROID_AVD_HOME}/${LATRY_ANDROID_AVD_NAME}.avd"
