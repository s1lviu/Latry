#!/bin/sh

set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-}
device_serial=${2:-${ANDROID_SERIAL:-}}

if [ -z "${build_dir}" ]; then
    echo "usage: $0 <android-build-dir> [device-serial]" >&2
    exit 2
fi

run_suite_step() {
    selector=$1
    echo "==> Running ${selector}"
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
        "${build_dir}" \
        "${device_serial}" \
        "${selector}"
}

run_suite_step "yo6say.latry.AndroidManifestInstrumentationTest"
run_suite_step "yo6say.latry.AndroidJniRegistrationInstrumentationTest"
run_suite_step "yo6say.latry.AudioHardwareInstrumentationTest"
run_suite_step "yo6say.latry.LatryMediaSessionManagerInstrumentationTest"

echo "==> Running Android foreground-service smoke tests"
/bin/sh "${repo_root}/scripts/run_android_service_smoke_tests.sh" "${build_dir}" "${device_serial}"

echo "Android device suite passed on ${device_serial:-default-device}"
