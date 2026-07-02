#!/bin/sh
# Shell fragment shared by the Android emulator helper scripts.

latry_prepend_path() {
    path_entry=$1
    if [ ! -d "${path_entry}" ]; then
        return 0
    fi
    case ":${PATH:-}:" in
        *":${path_entry}:"*) ;;
        *)
            if [ -n "${PATH:-}" ]; then
                PATH="${path_entry}:${PATH}"
            else
                PATH="${path_entry}"
            fi
            ;;
    esac
}

: "${LATRY_QT_ROOT:=/Volumes/SSD-Silviu/Qt}"
: "${LATRY_QT_VERSION:=6.11.0}"
: "${LATRY_QT_HOST_PATH:=${LATRY_QT_ROOT}/${LATRY_QT_VERSION}/macos}"
: "${LATRY_QT_ANDROID_ARM64_PATH:=${LATRY_QT_ROOT}/${LATRY_QT_VERSION}/android_arm64_v8a}"
: "${LATRY_QT_ANDROID_X86_64_PATH:=${LATRY_QT_ROOT}/${LATRY_QT_VERSION}/android_x86_64}"
: "${LATRY_ANDROID_SDK_ROOT:=/Volumes/SSD-Silviu/SDKs}"
: "${LATRY_ANDROID_NDK_ROOT:=${LATRY_ANDROID_SDK_ROOT}/ndk/27.2.12479018}"
: "${LATRY_ANDROID_USER_HOME:=/Volumes/SSD-Silviu/Android/.android}"
: "${LATRY_ANDROID_AVD_HOME:=${LATRY_ANDROID_USER_HOME}/avd}"
: "${LATRY_ANDROID_DEVICE_PROFILE:=pixel_8}"
: "${LATRY_ANDROID_PLATFORM:=latest}"
: "${LATRY_ANDROID_DEPLOY_PLATFORM:=android-36}"
: "${LATRY_ANDROIDDEPLOYQT:=${LATRY_QT_HOST_PATH}/bin/androiddeployqt}"
: "${LATRY_SDKMANAGER:=${LATRY_ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager}"
: "${LATRY_AVDMANAGER:=${LATRY_ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/avdmanager}"
: "${LATRY_EMULATOR_BIN:=${LATRY_ANDROID_SDK_ROOT}/emulator/emulator}"
: "${LATRY_ADB_BIN:=${LATRY_ANDROID_SDK_ROOT}/platform-tools/adb}"

if [ -z "${LATRY_ANDROID_EMULATOR_ABI:-}" ]; then
    case "$(uname -m 2>/dev/null || echo unknown)" in
        arm64|aarch64)
            LATRY_ANDROID_EMULATOR_ABI="arm64-v8a"
            ;;
        *)
            LATRY_ANDROID_EMULATOR_ABI="x86_64"
            ;;
    esac
fi

if [ -z "${LATRY_QT_ANDROID_EMULATOR_PATH:-}" ]; then
    case "${LATRY_ANDROID_EMULATOR_ABI}" in
        arm64-v8a)
            LATRY_QT_ANDROID_EMULATOR_PATH="${LATRY_QT_ANDROID_ARM64_PATH}"
            ;;
        x86_64)
            LATRY_QT_ANDROID_EMULATOR_PATH="${LATRY_QT_ANDROID_X86_64_PATH}"
            ;;
        *)
            LATRY_QT_ANDROID_EMULATOR_PATH="${LATRY_QT_ANDROID_ARM64_PATH}"
            ;;
    esac
fi

if [ -z "${LATRY_ANDROID_AVD_NAME:-}" ]; then
    case "${LATRY_ANDROID_EMULATOR_ABI}" in
        arm64-v8a)
            LATRY_ANDROID_AVD_NAME="latry_android16_api36_arm64"
            ;;
        x86_64)
            LATRY_ANDROID_AVD_NAME="latry_android16_api36_x86_64"
            ;;
        *)
            LATRY_ANDROID_AVD_NAME="latry_android16_api36"
            ;;
    esac
fi

if [ -z "${LATRY_ANDROID_SYSTEM_IMAGE:-}" ]; then
    LATRY_ANDROID_SYSTEM_IMAGE="system-images;android-36;google_apis;${LATRY_ANDROID_EMULATOR_ABI}"
fi

if [ -z "${LATRY_QT_CMAKE_EMULATOR:-}" ]; then
    LATRY_QT_CMAKE_EMULATOR="${LATRY_QT_ANDROID_EMULATOR_PATH}/bin/qt-cmake"
fi

: "${LATRY_QT_CMAKE_X86_64:=${LATRY_QT_ANDROID_X86_64_PATH}/bin/qt-cmake}"

if [ -z "${LATRY_JDK_PATH:-}" ]; then
    if [ -n "${JAVA_HOME:-}" ]; then
        LATRY_JDK_PATH="${JAVA_HOME}"
    elif [ -d "/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home" ]; then
        LATRY_JDK_PATH="/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"
    elif [ -d "/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home" ]; then
        LATRY_JDK_PATH="/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home"
    else
        LATRY_JDK_PATH=""
    fi
fi

export LATRY_QT_ROOT LATRY_QT_VERSION LATRY_QT_HOST_PATH
export LATRY_QT_ANDROID_ARM64_PATH LATRY_QT_ANDROID_X86_64_PATH LATRY_QT_ANDROID_EMULATOR_PATH
export LATRY_ANDROID_SDK_ROOT LATRY_ANDROID_NDK_ROOT
export LATRY_ANDROID_USER_HOME LATRY_ANDROID_AVD_HOME LATRY_ANDROID_AVD_NAME
export LATRY_ANDROID_SYSTEM_IMAGE LATRY_ANDROID_DEVICE_PROFILE LATRY_ANDROID_EMULATOR_ABI
export LATRY_ANDROID_PLATFORM LATRY_ANDROID_DEPLOY_PLATFORM
export LATRY_QT_CMAKE_EMULATOR LATRY_QT_CMAKE_X86_64 LATRY_ANDROIDDEPLOYQT
export LATRY_SDKMANAGER LATRY_AVDMANAGER LATRY_EMULATOR_BIN LATRY_ADB_BIN
export LATRY_JDK_PATH

export ANDROID_HOME="${LATRY_ANDROID_SDK_ROOT}"
export ANDROID_SDK_ROOT="${LATRY_ANDROID_SDK_ROOT}"
export ANDROID_USER_HOME="${LATRY_ANDROID_USER_HOME}"
export ANDROID_AVD_HOME="${LATRY_ANDROID_AVD_HOME}"

latry_prepend_path "${LATRY_ANDROID_SDK_ROOT}/platform-tools"
latry_prepend_path "${LATRY_ANDROID_SDK_ROOT}/cmdline-tools/latest/bin"
latry_prepend_path "${LATRY_ANDROID_SDK_ROOT}/emulator"
latry_prepend_path "${LATRY_QT_HOST_PATH}/bin"
latry_prepend_path "${LATRY_QT_ANDROID_EMULATOR_PATH}/bin"

export PATH
