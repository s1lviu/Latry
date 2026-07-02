#!/bin/sh

set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-}
device_serial=${2:-${ANDROID_SERIAL:-}}
test_selector_start="yo6say.latry.VoipBackgroundServiceInstrumentationTest#foregroundServiceStartsAndProcessesExternalControls"
test_selector_network="yo6say.latry.VoipBackgroundServiceInstrumentationTest#connectionMonitoringStartsDefaultNetworkMonitorAndDispatchesSnapshots"
test_selector_activity_finish="yo6say.latry.VoipBackgroundServiceInstrumentationTest#finishingActivityDoesNotStopLiveBackgroundService"
test_selector_loader_conflict="yo6say.latry.VoipBackgroundServiceInstrumentationTest#serviceSkipsQtAttachWhenActivityLoaderOccupiesQtSingletonBeforeQtMarkedStarted"
test_selector_notification="yo6say.latry.VoipBackgroundServiceInstrumentationTest#serviceNotificationIsLowImportanceAndOnlyAlertsOnce"

if [ -z "${build_dir}" ]; then
    echo "usage: $0 <android-build-dir> [device-serial]" >&2
    exit 2
fi

if [ ! -d "${build_dir}" ]; then
    echo "android build directory not found: ${build_dir}" >&2
    exit 2
fi

if [ -n "${device_serial}" ]; then
    export ANDROID_SERIAL="${device_serial}"
fi

PATH=/Volumes/SSD-Silviu/SDKs/platform-tools:${PATH} \
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
    "${build_dir}" "${device_serial}" "${test_selector_start}"

PATH=/Volumes/SSD-Silviu/SDKs/platform-tools:${PATH} \
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
    "${build_dir}" "${device_serial}" "${test_selector_network}"

PATH=/Volumes/SSD-Silviu/SDKs/platform-tools:${PATH} \
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
    "${build_dir}" "${device_serial}" "${test_selector_activity_finish}"

PATH=/Volumes/SSD-Silviu/SDKs/platform-tools:${PATH} \
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
    "${build_dir}" "${device_serial}" "${test_selector_loader_conflict}"

PATH=/Volumes/SSD-Silviu/SDKs/platform-tools:${PATH} \
    /bin/sh "${repo_root}/scripts/run_android_instrumented_tests.sh" \
    "${build_dir}" "${device_serial}" "${test_selector_notification}"

echo "Android foreground-service smoke tests passed on ${device_serial:-default-device}"
