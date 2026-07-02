#!/bin/sh

set -eu

repo_root=${1:-$(cd "$(dirname "$0")/.." && pwd)}

if ! command -v javac >/dev/null 2>&1; then
    echo "javac not found" >&2
    exit 1
fi

if ! command -v java >/dev/null 2>&1; then
    echo "java not found" >&2
    exit 1
fi

resolve_android_jar() {
    sdk_dir=$1
    latest_jar=
    for jar in "${sdk_dir}"/platforms/android-*/android.jar; do
        if [ -f "${jar}" ]; then
            latest_jar=${jar}
        fi
    done

    if [ -n "${latest_jar}" ]; then
        printf '%s\n' "${latest_jar}"
        return 0
    fi

    return 1
}

sdk_candidates=""
if [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -d "${ANDROID_SDK_ROOT}" ]; then
    sdk_candidates="${sdk_candidates}
${ANDROID_SDK_ROOT}"
fi
if [ -n "${ANDROID_HOME:-}" ] && [ -d "${ANDROID_HOME}" ]; then
    sdk_candidates="${sdk_candidates}
${ANDROID_HOME}"
fi
local_properties="${repo_root}/build-android-arm64-qt610/android-build/local.properties"
if [ -f "${local_properties}" ]; then
    sdk_dir=$(sed -n 's/^sdk\.dir=//p' "${local_properties}" | head -n 1)
    if [ -n "${sdk_dir}" ] && [ -d "${sdk_dir}" ]; then
        sdk_candidates="${sdk_candidates}
${sdk_dir}"
    fi
fi
if [ -d "/Volumes/SSD-Silviu/SDKs" ]; then
    sdk_candidates="${sdk_candidates}
/Volumes/SSD-Silviu/SDKs"
fi

sdk_dir=""
android_jar=""
for candidate in ${sdk_candidates}; do
    android_jar=$(resolve_android_jar "${candidate}" || true)
    if [ -n "${android_jar}" ]; then
        sdk_dir=${candidate}
        break
    fi
done

if [ -z "${android_jar}" ]; then
    echo "android.jar not found under any known Android SDK path" >&2
    exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/latry-android-java-tests.XXXXXX")
trap 'rm -rf "${tmpdir}"' EXIT INT TERM

classes_dir="${tmpdir}/classes"
mkdir -p "${classes_dir}"

javac -d "${classes_dir}" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioRoutePolicy.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioCaptureProfile.java" \
    "${repo_root}/android/src/yo6say/latry/VoipServiceLifecyclePolicy.java" \
    "${repo_root}/android/src/yo6say/latry/VoipServiceStatusFormatter.java" \
    "${repo_root}/android/unitTest/yo6say/latry/LatryAudioRoutePolicySelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/LatryAudioCaptureProfileSelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/VoipServiceLifecyclePolicySelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/VoipServiceStatusFormatterSelfTest.java"

java -cp "${classes_dir}" yo6say.latry.LatryAudioRoutePolicySelfTest
java -cp "${classes_dir}" yo6say.latry.LatryAudioCaptureProfileSelfTest
java -cp "${classes_dir}" yo6say.latry.VoipServiceLifecyclePolicySelfTest
java -cp "${classes_dir}" yo6say.latry.VoipServiceStatusFormatterSelfTest

boot_classes_dir="${tmpdir}/boot-classes"
mkdir -p "${boot_classes_dir}"

javac -d "${boot_classes_dir}" \
    "${repo_root}/android/unitTestStubs/android/R.java" \
    "${repo_root}/android/unitTestStubs/android/app/Notification.java" \
    "${repo_root}/android/unitTestStubs/android/app/NotificationChannel.java" \
    "${repo_root}/android/unitTestStubs/android/app/NotificationManager.java" \
    "${repo_root}/android/unitTestStubs/android/app/PendingIntent.java" \
    "${repo_root}/android/unitTestStubs/android/content/BroadcastReceiver.java" \
    "${repo_root}/android/unitTestStubs/android/content/Context.java" \
    "${repo_root}/android/unitTestStubs/android/content/Intent.java" \
    "${repo_root}/android/unitTestStubs/android/content/pm/ApplicationInfo.java" \
    "${repo_root}/android/unitTestStubs/android/os/Build.java" \
    "${repo_root}/android/unitTestStubs/android/os/Handler.java" \
    "${repo_root}/android/unitTestStubs/android/os/Looper.java" \
    "${repo_root}/android/unitTestStubs/android/content/SharedPreferences.java" \
    "${repo_root}/android/unitTestStubs/android/util/Log.java" \
    "${repo_root}/android/unitTestStubs/android/view/KeyEvent.java" \
    "${repo_root}/android/unitTestStubs/yo6say/latry/LatryActivity.java" \
    "${repo_root}/android/unitTestStubs/yo6say/latry/VoipBackgroundService.java" \
    "${repo_root}/android/src/yo6say/latry/ConnectionProfileStore.java" \
    "${repo_root}/android/src/yo6say/latry/HardwarePttSettingsStore.java" \
    "${repo_root}/android/src/yo6say/latry/HardwarePttKeyPolicy.java" \
    "${repo_root}/android/src/yo6say/latry/HardwarePttLearningCoordinator.java" \
    "${repo_root}/android/src/yo6say/latry/NotificationActionReceiver.java" \
    "${repo_root}/android/src/yo6say/latry/PTTButtonBroadcastReceiver.java" \
    "${repo_root}/android/unitTest/yo6say/latry/FakeSharedPreferences.java" \
    "${repo_root}/android/unitTest/yo6say/latry/FakeContext.java" \
    "${repo_root}/android/unitTest/yo6say/latry/ConnectionProfileStoreSelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/HardwarePttSettingsStoreSelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/HardwarePttKeyPolicySelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/HardwarePttLearningCoordinatorSelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/NotificationActionReceiverSelfTest.java" \
    "${repo_root}/android/unitTest/yo6say/latry/PTTButtonBroadcastReceiverSelfTest.java"

java -cp "${boot_classes_dir}" yo6say.latry.ConnectionProfileStoreSelfTest
java -cp "${boot_classes_dir}" yo6say.latry.HardwarePttSettingsStoreSelfTest
java -cp "${boot_classes_dir}" yo6say.latry.HardwarePttKeyPolicySelfTest
java -cp "${boot_classes_dir}" yo6say.latry.HardwarePttLearningCoordinatorSelfTest
java -cp "${boot_classes_dir}" yo6say.latry.NotificationActionReceiverSelfTest
java -cp "${boot_classes_dir}" yo6say.latry.PTTButtonBroadcastReceiverSelfTest

javac -cp "${android_jar}" -d "${classes_dir}" \
    "${repo_root}/android/unitTestStubs/androidx/core/content/ContextCompat.java" \
    "${repo_root}/android/src/yo6say/latry/AndroidIntegrationTestHooks.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioCaptureProfile.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioRoutePolicy.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioRouteManager.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioRecordInput.java" \
    "${repo_root}/android/src/yo6say/latry/LatryAudioTrackPlayer.java" \
    "${repo_root}/android/src/yo6say/latry/NetworkHandoverMonitor.java" \
    "${repo_root}/android/src/yo6say/latry/VoipServiceStatusFormatter.java"

echo "Android Java route tests passed"
