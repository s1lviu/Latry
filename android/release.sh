#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

qt_root=${LATRY_QT_ROOT:-/Volumes/SSD-Silviu/Qt}
qt_version=${LATRY_QT_VERSION:-6.11.0}
qt_host_path=${LATRY_QT_HOST_PATH:-${qt_root}/${qt_version}/macos}
qt_android_arm64_path=${LATRY_QT_ANDROID_ARM64_PATH:-${qt_root}/${qt_version}/android_arm64_v8a}
qt_android_armv7_path=${LATRY_QT_ANDROID_ARMV7_PATH:-${qt_root}/${qt_version}/android_armv7}
qt_cmake=${LATRY_QT_CMAKE:-${qt_android_arm64_path}/bin/qt-cmake}
qt_host_cmake_dir=${qt_host_path}/lib/cmake
androiddeployqt=${LATRY_ANDROIDDEPLOYQT:-${qt_host_path}/bin/androiddeployqt}

android_sdk_root=${LATRY_ANDROID_SDK_ROOT:-/Volumes/SSD-Silviu/SDKs}
android_ndk_root=${LATRY_ANDROID_NDK_ROOT:-${android_sdk_root}/ndk/27.2.12479018}
android_platform=${LATRY_ANDROID_PLATFORM:-android-28}
android_deploy_platform=${LATRY_ANDROID_DEPLOY_PLATFORM:-android-36}

jdk_path=${LATRY_JDK_PATH:-${JAVA_HOME:-}}
if [ -z "${jdk_path}" ] && [ -d "/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home" ]; then
    jdk_path="/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home"
fi

build_dir=${LATRY_BUILD_DIR:-${repo_root}/build-android-play-release}
gradle_user_home=${LATRY_GRADLE_USER_HOME:-${repo_root}/.gradle-home-release}
app_target=${LATRY_ANDROID_APP_TARGET:-applatry}
android_abis=${LATRY_ANDROID_ABIS:-arm64-v8a;armeabi-v7a}
desktop_dir="${HOME}/Desktop"

jobs_default=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
if [ -z "${jobs_default}" ]; then
    jobs_default=$(sysctl -n hw.logicalcpu 2>/dev/null || true)
fi
if [ -z "${jobs_default}" ]; then
    jobs_default=8
fi
jobs=${LATRY_JOBS:-${jobs_default}}
keep_build_dir=${LATRY_KEEP_BUILD_DIR:-0}

keystore_path=${LATRY_ANDROID_KEYSTORE_PATH:-}
keystore_alias=${LATRY_ANDROID_KEYSTORE_ALIAS:-}
store_pass=${LATRY_ANDROID_KEYSTORE_STORE_PASS:-}
key_pass=${LATRY_ANDROID_KEYSTORE_KEY_PASS:-}
keystore_type=${LATRY_ANDROID_KEYSTORE_TYPE:-}

usage() {
    cat <<EOF
usage: ./release.sh

Builds a signed Play Store .aab, verifies the embedded R8 metadata,
and copies the release artifacts to ~/Desktop/Latry-<versionName>-build<versionCode>/
  latry-release.aab
  mapping.txt
  native-debug-symbols.zip
  latry-universal.apk  (if bundletool is installed)

Environment overrides:
  LATRY_BUILD_DIR
  LATRY_QT_ROOT
  LATRY_QT_VERSION
  LATRY_QT_HOST_PATH
  LATRY_QT_ANDROID_ARM64_PATH
  LATRY_QT_ANDROID_ARMV7_PATH
  LATRY_QT_CMAKE
  LATRY_ANDROIDDEPLOYQT
  LATRY_ANDROID_SDK_ROOT
  LATRY_ANDROID_NDK_ROOT
  LATRY_ANDROID_PLATFORM
  LATRY_ANDROID_DEPLOY_PLATFORM
  LATRY_JDK_PATH
  LATRY_JOBS
  LATRY_KEEP_BUILD_DIR
  LATRY_ANDROID_ABIS
  LATRY_ANDROID_KEYSTORE_PATH
  LATRY_ANDROID_KEYSTORE_ALIAS
  LATRY_ANDROID_KEYSTORE_STORE_PASS
  LATRY_ANDROID_KEYSTORE_KEY_PASS
  LATRY_ANDROID_KEYSTORE_TYPE
EOF
}

fail() {
    echo "$*" >&2
    exit 1
}

require_path() {
    path_value=$1
    description=$2
    if [ ! -e "${path_value}" ]; then
        fail "${description} not found: ${path_value}"
    fi
}

safe_remove_tree() {
    path_value=$1
    description=$2

    if [ -z "${path_value}" ] || [ "${path_value}" = "/" ]; then
        fail "Refusing to remove unsafe ${description}: ${path_value}"
    fi

    if [ ! -e "${path_value}" ]; then
        return
    fi

    rm -rf "${path_value}"
}

cleanup_release_build_outputs() {
    if [ "${keep_build_dir}" = "1" ]; then
        echo "==> Keeping release build directory: ${build_dir}"
        return
    fi

    case "${build_dir}" in
        "${repo_root}"/build-* ) ;;
        * )
            fail "Refusing to remove unexpected build directory: ${build_dir}"
            ;;
    esac

    safe_remove_tree "${build_dir}" "release build directory"

    case "${gradle_user_home}" in
        "${repo_root}"/.gradle-home-* )
            safe_remove_tree "${gradle_user_home}" "Gradle user home"
            ;;
    esac

    echo "==> Cleaned release build outputs"
}

prompt_value() {
    prompt_label=$1
    current_value=$2
    if [ -n "${current_value}" ]; then
        printf '%s' "${current_value}"
        return
    fi

    if [ ! -t 0 ]; then
        fail "${prompt_label} is required in non-interactive mode"
    fi

    printf "%s: " "${prompt_label}" >&2
    IFS= read -r entered_value
    if [ -z "${entered_value}" ]; then
        fail "${prompt_label} is required"
    fi
    printf '%s' "${entered_value}"
}

prompt_secret() {
    prompt_label=$1
    current_value=$2
    if [ -n "${current_value}" ]; then
        printf '%s' "${current_value}"
        return
    fi

    if [ ! -t 0 ]; then
        fail "${prompt_label} is required in non-interactive mode"
    fi

    printf "%s: " "${prompt_label}" >&2
    stty -echo
    IFS= read -r entered_value
    stty echo
    printf '\n' >&2
    if [ -z "${entered_value}" ]; then
        fail "${prompt_label} is required"
    fi
    printf '%s' "${entered_value}"
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

if [ $# -ne 0 ]; then
    usage >&2
    exit 2
fi

if [ -z "${keystore_path}" ] && [ -f "${repo_root}/android_release_jks.keystore" ]; then
    # Prefer the explicit JKS file in the repo. Override LATRY_ANDROID_KEYSTORE_PATH to use another one.
    keystore_path="${repo_root}/android_release_jks.keystore"
fi

if [ -n "${keystore_path}" ] && [ -z "${keystore_type}" ] \
    && [ "$(basename "${keystore_path}")" = "android_release_jks.keystore" ]; then
    keystore_type=JKS
fi

keystore_alias=$(prompt_value "Keystore alias" "${keystore_alias}")
store_pass=$(prompt_secret "Keystore password" "${store_pass}")
if [ -z "${key_pass}" ]; then
    key_pass=${store_pass}
fi

require_path "${qt_cmake}" "Qt Android qt-cmake"
require_path "${androiddeployqt}" "androiddeployqt"
require_path "${qt_host_path}" "Qt host path"
require_path "${qt_android_arm64_path}" "Qt Android arm64 kit"
require_path "${qt_android_armv7_path}" "Qt Android armv7 kit"
require_path "${android_sdk_root}" "Android SDK root"
require_path "${android_ndk_root}" "Android NDK root"
require_path "${jdk_path}" "JDK path"
require_path "${keystore_path}" "Keystore"

if ! command -v cmake >/dev/null 2>&1; then
    fail "cmake not found in PATH"
fi

if ! command -v ninja >/dev/null 2>&1; then
    fail "ninja not found in PATH"
fi

mkdir -p "${build_dir}" "${gradle_user_home}"

# Wipe stale CMake caches from secondary ABI sub-builds (--fresh only
# cleans the top-level configure, not Qt's multi-ABI sub-projects).
rm -rf "${build_dir}/android_abi_builds"

export JAVA_HOME="${jdk_path}"
export GRADLE_USER_HOME="${gradle_user_home}"

deployment_file="${build_dir}/android-${app_target}-deployment-settings.json"
android_build_dir="${build_dir}/android-build-${app_target}"

echo "==> Configuring multi-ABI release build in ${build_dir}"
"${qt_cmake}" \
    --fresh \
    -S "${repo_root}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_SDK_ROOT="${android_sdk_root}" \
    -DANDROID_NDK_ROOT="${android_ndk_root}" \
    -DANDROID_PLATFORM="${android_platform}" \
    -DQT_HOST_PATH="${qt_host_path}" \
    -DQT_HOST_PATH_CMAKE_DIR="${qt_host_cmake_dir}" \
    -DQT_USE_TARGET_ANDROID_BUILD_DIR=ON \
    "-DQT_ANDROID_ABIS=${android_abis}" \
    "-DQT_PATH_ANDROID_ABI_armeabi-v7a=${qt_android_armv7_path}"

# androiddeployqt reuses the package directory and does not remove stale
# packaged outputs between runs, so reset it before re-staging the release.
safe_remove_tree "${android_build_dir}" "stale Android package directory"

echo "==> Building Qt Android package inputs"
cmake --build "${build_dir}" --parallel "${jobs}" --target "${app_target}_prepare_apk_dir"

require_path "${deployment_file}" "Android deployment settings file"
require_path "${android_build_dir}" "Android build directory"
require_path "${android_build_dir}/libs/arm64-v8a" "arm64-v8a packaged libraries"
require_path "${android_build_dir}/libs/armeabi-v7a" "armeabi-v7a packaged libraries"

echo "==> Packaging signed Play Store bundle"
set -- \
    "${androiddeployqt}" \
    --input "${deployment_file}" \
    --output "${android_build_dir}" \
    --apk "${android_build_dir}/${app_target}.apk" \
    --aab \
    --gradle \
    --release \
    --android-platform "${android_deploy_platform}" \
    --jdk "${jdk_path}" \
    --sign "${keystore_path}" "${keystore_alias}" \
    --storepass "${store_pass}" \
    --keypass "${key_pass}" \
    --verbose

if [ -n "${keystore_type}" ]; then
    set -- "$@" --storetype "${keystore_type}"
fi

"$@"

aab_path=$(find "${android_build_dir}" -type f -path "*/build/outputs/bundle/release/*.aab" -print | head -n 1)
if [ -z "${aab_path}" ]; then
    aab_path=$(find "${android_build_dir}" -type f -name "*.aab" -print | head -n 1)
fi

if [ -z "${aab_path}" ]; then
    fail "No .aab produced under ${android_build_dir}"
fi

# Read version from CMakeLists.txt (single source of truth)
version_name=$(sed -n 's/^project(latry VERSION \([^ ]*\) .*/\1/p' "${repo_root}/CMakeLists.txt")
version_code=$(sed -n 's/^set(LATRY_VERSION_CODE \([0-9]*\))/\1/p' "${repo_root}/CMakeLists.txt")
if [ -z "${version_name}" ]; then
    fail "Could not read version name from CMakeLists.txt"
fi
if [ -z "${version_code}" ]; then
    fail "Could not read version code from CMakeLists.txt"
fi

release_tag="Latry-${version_name}-build${version_code}"
release_dir="${desktop_dir}/${release_tag}"
mkdir -p "${release_dir}"

final_aab="${release_dir}/latry-release.aab"
cp -f "${aab_path}" "${final_aab}"
echo "==> AAB: ${final_aab}"

mapping_path=$(find "${android_build_dir}" -type f -path "*/build/outputs/mapping/release/mapping.txt" -print | head -n 1)
if [ -z "${mapping_path}" ]; then
    mapping_path=$(find "${android_build_dir}" -type f -name "mapping.txt" -print | head -n 1)
fi
if [ -z "${mapping_path}" ]; then
    fail "No R8/ProGuard mapping.txt produced under ${android_build_dir}"
fi

if ! grep -q '^# compiler: R8$' "${mapping_path}"; then
    fail "The generated mapping.txt was not produced by R8"
fi

if ! zipinfo -1 "${final_aab}" | grep -q '^BUNDLE-METADATA/com.android.tools.build.obfuscation/proguard.map$'; then
    fail "The generated AAB does not embed ProGuard/R8 bundle metadata"
fi

final_mapping="${release_dir}/mapping.txt"
cp -f "${mapping_path}" "${final_mapping}"
echo "==> Mapping: ${final_mapping}"

native_symbols_path=$(find "${android_build_dir}" -type f -path "*/build/outputs/native-debug-symbols/release/*.zip" -print | head -n 1)
if [ -n "${native_symbols_path}" ]; then
    final_native_symbols="${release_dir}/native-debug-symbols.zip"
    cp -f "${native_symbols_path}" "${final_native_symbols}"
    echo "==> Native symbols: ${final_native_symbols}"
fi

# Build a universal sideloadable APK from the bundle
if command -v bundletool >/dev/null 2>&1; then
    echo "==> Building universal APK for sideloading"
    tmp_apks=$(mktemp /tmp/latry-apks.XXXXXX.apks)
    rm -f "${tmp_apks}"
    bundletool build-apks \
        --bundle="${final_aab}" \
        --output="${tmp_apks}" \
        --mode=universal \
        --ks="${keystore_path}" \
        --ks-key-alias="${keystore_alias}" \
        --ks-pass="pass:${store_pass}" \
        --key-pass="pass:${key_pass}"
    unzip -o -q "${tmp_apks}" universal.apk -d /tmp
    final_apk="${release_dir}/latry-universal.apk"
    mv -f /tmp/universal.apk "${final_apk}"
    rm -f "${tmp_apks}"
    echo "==> APK: ${final_apk}"
else
    echo "==> Skipping APK: install bundletool (brew install bundletool) to also produce a sideloadable APK"
fi

cleanup_release_build_outputs

echo "==> Done — ${release_dir}"
