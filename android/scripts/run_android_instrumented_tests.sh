#!/bin/sh

set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-}
device_serial=${2:-${ANDROID_SERIAL:-}}
test_selector=${3:-${LATRY_ANDROID_TEST_CLASS:-}}

if [ -z "${build_dir}" ]; then
    echo "usage: $0 <android-build-dir> [device-serial] [test-class-or-method]" >&2
    exit 2
fi

if [ ! -d "${build_dir}" ]; then
    echo "android build directory not found: ${build_dir}" >&2
    exit 2
fi

if [ ! -f "${build_dir}/build.gradle" ]; then
    echo "Gradle build file not found in ${build_dir}" >&2
    exit 2
fi

if [ ! -x "${build_dir}/gradlew" ]; then
    echo "Gradle wrapper not found in ${build_dir}" >&2
    exit 2
fi

if ! command -v rsync >/dev/null 2>&1; then
    echo "rsync not found" >&2
    exit 1
fi

if ! command -v adb >/dev/null 2>&1; then
    echo "adb not found" >&2
    exit 1
fi

if [ -z "${JAVA_HOME:-}" ] && [ -d "/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home" ]; then
    export JAVA_HOME="/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home"
fi

if [ -n "${device_serial}" ]; then
    export ANDROID_SERIAL="${device_serial}"
fi

resolve_deployment_setting() {
    key=$1
    file=$2
    sed -n "s/^[[:space:]]*\"${key}\": \"\\([^\"]*\\)\".*/\\1/p" "${file}" | head -n 1
}

rsync -a --delete "${repo_root}/android/src/" "${build_dir}/src/"
mkdir -p "${build_dir}/src/androidTest/java"
rsync -a --delete "${repo_root}/android/androidTest/java/" "${build_dir}/src/androidTest/java/"
cp "${repo_root}/android/AndroidManifest.xml" "${build_dir}/AndroidManifest.xml"

if grep -q '%%INSERT_VERSION_' "${build_dir}/AndroidManifest.xml"; then
    build_dir_name=$(basename "${build_dir}")
    deployment_target=$(printf '%s\n' "${build_dir_name}" | sed 's/^android-build-//')
    deployment_settings="${build_dir%/}/../android-${deployment_target}-deployment-settings.json"

    if [ ! -f "${deployment_settings}" ]; then
        echo "android deployment settings not found: ${deployment_settings}" >&2
        exit 2
    fi

    version_code=$(resolve_deployment_setting "android-version-code" "${deployment_settings}")
    version_name=$(resolve_deployment_setting "android-version-name" "${deployment_settings}")

    if [ -z "${version_code}" ] || [ -z "${version_name}" ]; then
        echo "failed to resolve android version placeholders from ${deployment_settings}" >&2
        exit 2
    fi

    tmp_manifest=$(mktemp "${TMPDIR:-/tmp}/latry-android-manifest.XXXXXX")
    sed \
        -e "s|-- %%INSERT_VERSION_CODE%% --|${version_code}|g" \
        -e "s|-- %%INSERT_VERSION_NAME%% --|${version_name}|g" \
        "${build_dir}/AndroidManifest.xml" > "${tmp_manifest}"
    mv "${tmp_manifest}" "${build_dir}/AndroidManifest.xml"
fi

build_gradle="${build_dir}/build.gradle"
if ! grep -q "LATRY_INSTRUMENTATION_TESTS" "${build_gradle}"; then
    tmp_file=$(mktemp "${TMPDIR:-/tmp}/latry-gradle.XXXXXX")
    awk '
        /^dependencies[[:space:]]*\{/ && !deps_done {
            print
            print "    // LATRY_INSTRUMENTATION_TESTS"
            print "    androidTestImplementation '\''junit:junit:4.13.2'\''"
            print "    androidTestImplementation '\''androidx.test:runner:1.6.2'\''"
            print "    androidTestImplementation '\''androidx.test:core:1.6.1'\''"
            print "    androidTestImplementation '\''androidx.test.ext:junit:1.2.1'\''"
            deps_done = 1
            next
        }
        /^[[:space:]]*defaultConfig[[:space:]]*\{/ && !runner_done {
            print
            print "        testInstrumentationRunner \"androidx.test.runner.AndroidJUnitRunner\""
            runner_done = 1
            next
        }
        {
            print
        }
        END {
            if (!deps_done || !runner_done) {
                exit 2
            }
        }
    ' "${build_gradle}" > "${tmp_file}"
    mv "${tmp_file}" "${build_gradle}"
fi

if ! grep -q "LATRY_INSTRUMENTATION_SOURCESET" "${build_gradle}"; then
    tmp_file=$(mktemp "${TMPDIR:-/tmp}/latry-gradle.XXXXXX")
    awk '
        /^[[:space:]]*main[[:space:]]*\{/ {
            in_main = 1
            print
            next
        }
        in_main && /java\.srcDirs = \[qtAndroidDir \+ '\''\/src'\'', '\''src'\'', '\''java'\''\]/ && !exclude_done {
            print
            print "            // LATRY_INSTRUMENTATION_SOURCESET"
            print "            java.exclude '\''androidTest/**'\''"
            exclude_done = 1
            next
        }
        in_main && /^[[:space:]]*}[[:space:]]*$/ && !android_test_done {
            print
            print "        androidTest {"
            print "            java.srcDirs = ['\''src/androidTest/java'\'']"
            print "        }"
            android_test_done = 1
            in_main = 0
            next
        }
        {
            print
        }
        END {
            if (!exclude_done || !android_test_done) {
                exit 2
            }
        }
    ' "${build_gradle}" > "${tmp_file}"
    mv "${tmp_file}" "${build_gradle}"
fi

adb devices
adb shell am force-stop yo6say.latry || true
adb shell am force-stop yo6say.latry.test || true
adb uninstall yo6say.latry || true
adb uninstall yo6say.latry.test || true
adb logcat -c

cd "${build_dir}"
set -- ./gradlew --no-daemon
if [ -n "${test_selector}" ]; then
    echo "Running connectedDebugAndroidTest for ${test_selector}"
    set -- "$@" "-Pandroid.testInstrumentationRunnerArguments.class=${test_selector}"
fi
set -- "$@" connectedDebugAndroidTest
exec "$@"
