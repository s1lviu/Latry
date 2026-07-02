#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/.." && pwd)
. "${script_dir}/android_ssd_env.sh"

usage() {
    cat <<EOF
usage: ./scripts/build_android_emulator_debug.sh

Builds a single-ABI debug APK with Qt 6.11.0 for the Android 16 emulator on the SSD.
Default emulator ABI on Apple Silicon: arm64-v8a.
If an emulator is already running, the script also installs the APK with adb.
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

jobs_default=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
if [ -z "${jobs_default}" ]; then
    jobs_default=$(sysctl -n hw.logicalcpu 2>/dev/null || true)
fi
if [ -z "${jobs_default}" ]; then
    jobs_default=8
fi

build_dir=${LATRY_BUILD_DIR:-${repo_root}/build-android-emulator-debug}
gradle_user_home=${LATRY_GRADLE_USER_HOME:-${repo_root}/.gradle-home-emulator}
app_target=${LATRY_ANDROID_APP_TARGET:-applatry}
jobs=${LATRY_JOBS:-${jobs_default}}
qt_host_cmake_dir=${LATRY_QT_HOST_PATH}/lib/cmake
android_build_dir="${build_dir}/android-build-${app_target}"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

if [ $# -ne 0 ]; then
    usage >&2
    exit 2
fi

require_path "${LATRY_QT_CMAKE_EMULATOR}" "Qt emulator qt-cmake"
require_path "${LATRY_ANDROIDDEPLOYQT}" "androiddeployqt"
require_path "${LATRY_QT_HOST_PATH}" "Qt host path"
require_path "${qt_host_cmake_dir}" "Qt host CMake dir"
require_path "${LATRY_QT_ANDROID_EMULATOR_PATH}" "Qt Android emulator kit"
require_path "${LATRY_ANDROID_SDK_ROOT}" "Android SDK root"
require_path "${LATRY_ANDROID_NDK_ROOT}" "Android NDK root"

if [ -z "${LATRY_JDK_PATH}" ]; then
    echo "JDK path not found. Set LATRY_JDK_PATH or JAVA_HOME." >&2
    exit 1
fi
require_path "${LATRY_JDK_PATH}" "JDK path"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found in PATH" >&2
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "ninja not found in PATH" >&2
    exit 1
fi

mkdir -p "${build_dir}" "${gradle_user_home}"

export JAVA_HOME="${LATRY_JDK_PATH}"
export GRADLE_USER_HOME="${gradle_user_home}"

# Host include/linker flags from the interactive shell break the Android NDK toolchain.
unset CPPFLAGS
unset CFLAGS
unset CXXFLAGS
unset LDFLAGS
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset SDKROOT
unset MACOSX_DEPLOYMENT_TARGET

echo "==> Configuring ${LATRY_ANDROID_EMULATOR_ABI} Android debug build in ${build_dir}"
"${LATRY_QT_CMAKE_EMULATOR}" \
    --fresh \
    -S "${repo_root}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_SDK_ROOT="${LATRY_ANDROID_SDK_ROOT}" \
    -DANDROID_NDK_ROOT="${LATRY_ANDROID_NDK_ROOT}" \
    -DANDROID_PLATFORM="${LATRY_ANDROID_PLATFORM}" \
    -DQT_HOST_PATH="${LATRY_QT_HOST_PATH}" \
    -DQT_HOST_PATH_CMAKE_DIR="${qt_host_cmake_dir}" \
    -DQT_USE_TARGET_ANDROID_BUILD_DIR=ON \
    "-DQT_ANDROID_ABIS=${LATRY_ANDROID_EMULATOR_ABI}"

# androiddeployqt reuses the package directory and does not remove native
# libraries that disappeared from deployment settings between builds.
rm -rf "${android_build_dir}"

echo "==> Building Qt Android package inputs"
cmake --build "${build_dir}" --parallel "${jobs}" --target "${app_target}_prepare_apk_dir"

deployment_file="${build_dir}/android-${app_target}-deployment-settings.json"
apk_path="${android_build_dir}/${app_target}-debug.apk"

require_path "${deployment_file}" "Android deployment settings file"
require_path "${android_build_dir}" "Android build directory"

echo "==> Packaging debug APK"
"${LATRY_ANDROIDDEPLOYQT}" \
    --input "${deployment_file}" \
    --output "${android_build_dir}" \
    --apk "${apk_path}" \
    --gradle \
    --debug \
    --android-platform "${LATRY_ANDROID_DEPLOY_PLATFORM}"

echo "APK ready: ${apk_path}"

serial=$(pick_emulator_serial || true)
if [ -n "${serial}" ]; then
    echo "==> Installing on ${serial}"
    "${LATRY_ADB_BIN}" -s "${serial}" install -r "${apk_path}"
else
    echo "No running emulator detected. Start it with ./scripts/start_android16_emulator.sh"
fi
