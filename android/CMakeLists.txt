cmake_minimum_required(VERSION 3.16)

project(latry VERSION 0.0.18 LANGUAGES CXX)

set(LATRY_VERSION_NAME "${PROJECT_VERSION}")
set(LATRY_VERSION_CODE 32)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
include(CTest)

set(LATRY_CORE_SOURCES
    ReflectorClient.cpp
    ReflectorClientConnection.cpp
    ReflectorClientProtocol.cpp
    ReflectorClientUdp.cpp
    ReflectorClientPtt.cpp
    ReflectorClientRecovery.cpp
    ReflectorClientJni.cpp
    AudioEngine.cpp
    AudioEngineRecording.cpp
    AudioEnginePlayback.cpp
    AudioEngineFocus.cpp
    AndroidAudioRecordInput.cpp
    AndroidAudioTrackOutput.cpp
    AndroidAudioRouteInterop.cpp
    AudioLimiter.cpp
    AudioJitterBuffer.cpp
    AudioStreamDevice.cpp
    OpusWrapper.cpp
    Resampler.cpp
)

set(LATRY_APP_SOURCES
    main.cpp
    ${LATRY_CORE_SOURCES}
    BatteryOptimizationHandler.cpp
    SppPttBridge.cpp
    ReflectorClientSpp.cpp
)

# --- Find/Build All Dependencies FIRST ---

# 1. Find Qt6 - This MUST come before any qt_* commands.
find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Network Multimedia Bluetooth)
qt_policy(SET QTP0002 NEW)
if(ANDROID)
    # The project builds both the main app and a dedicated Qt Android service binary.
    # Use per-target Android build directories so deployment artifacts do not collide.
    set(QT_USE_TARGET_ANDROID_BUILD_DIR ON CACHE BOOL
        "Use per-target Android build directories for Qt Android deployment" FORCE)

    # AndroidExtras is optional in Qt 6.5+ where APIs moved to QtCore
    find_package(Qt6AndroidExtras QUIET)
    # CorePrivate needed for QtAndroidPrivate permission APIs (qandroidextras_p.h)
    find_package(Qt6 COMPONENTS CorePrivate QUIET)

    # Add OpenSSL support via KDAB
       include(FetchContent)
       FetchContent_Declare(
           android_openssl
           DOWNLOAD_EXTRACT_TIMESTAMP true
           URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
       )
       FetchContent_MakeAvailable(android_openssl)
       include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)

endif()

# 2. Find/Build Opus (Platform-Aware)
if(ANDROID)
    # --- ANDROID ---
    message(STATUS "Android build detected. Building Opus from 3rdparty/opus")
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "Disable Opus tests" FORCE)
    set(OPUS_BUILD_DEMO OFF CACHE BOOL "Disable Opus demo" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Disable Opus programs" FORCE)

    # Add the Opus source directory. This creates the 'opus' library target.
    add_subdirectory(3rdparty/opus)

    # Use the global include_directories command to make the Opus headers
    # visible to ALL subsequent commands in this directory, including AUTOMOC.
    include_directories("${opus_SOURCE_DIR}/include")

else()
    # --- DESKTOP (Linux/macOS) ---
    message(STATUS "Desktop build detected. Searching for system Opus library")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus)
    endif()
    find_path(OPUS_INCLUDE_DIR
        NAMES opus/opus.h
        PATHS ${OPUS_INCLUDE_DIRS} /opt/homebrew/include /usr/local/include /usr/include
    )
    find_library(OPUS_LIBRARY
        NAMES opus
        PATHS ${OPUS_LIBRARY_DIRS} /opt/homebrew/lib /usr/local/lib /usr/lib
    )

    if (NOT OPUS_INCLUDE_DIR OR NOT OPUS_LIBRARY)
        message(FATAL_ERROR "Opus library not found. Please install libopus-dev")
    endif()

    include_directories(${OPUS_INCLUDE_DIR})
    if (EXISTS "${OPUS_INCLUDE_DIR}/opus/opus.h")
        include_directories("${OPUS_INCLUDE_DIR}/opus")
    endif()
    # No external resampler dependency is required
endif()


# --- Define the Application Target ---
qt_add_executable(applatry
    ${LATRY_APP_SOURCES}
    qml.qrc
)

# --- Configure the Target's Links ---
target_link_libraries(applatry PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
    Qt6::Bluetooth
)

if(ANDROID)
    if(TARGET Qt6::AndroidExtras)
        target_link_libraries(applatry PRIVATE Qt6::AndroidExtras)
    endif()
    # CorePrivate needed for QtAndroidPrivate permission APIs (qandroidextras_p.h)
    if(TARGET Qt6::CorePrivate)
        target_link_libraries(applatry PRIVATE Qt6::CorePrivate)
    else()
        # Fallback: manually add private include path
        get_target_property(_qt_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
        foreach(_dir IN LISTS _qt_core_includes)
            if(_dir MATCHES "include/QtCore$" AND EXISTS "${_dir}/${Qt6_VERSION}")
                target_include_directories(applatry PRIVATE "${_dir}/${Qt6_VERSION}")
                break()
            endif()
        endforeach()
    endif()
    target_link_libraries(applatry PRIVATE opus)

    qt_add_executable(latryservice
        service_main.cpp
        ${LATRY_CORE_SOURCES}
    )
    target_link_libraries(latryservice PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Multimedia
        opus
    )
    if(TARGET Qt6::AndroidExtras)
        target_link_libraries(latryservice PRIVATE Qt6::AndroidExtras)
    endif()
    if(TARGET Qt6::CorePrivate)
        target_link_libraries(latryservice PRIVATE Qt6::CorePrivate)
    else()
        get_target_property(_qt_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
        foreach(_dir IN LISTS _qt_core_includes)
            if(_dir MATCHES "include/QtCore$" AND EXISTS "${_dir}/${Qt6_VERSION}")
                target_include_directories(latryservice PRIVATE "${_dir}/${Qt6_VERSION}")
                break()
            endif()
        endforeach()
    endif()
    target_compile_definitions(latryservice PRIVATE
        LATRY_VERSION_NAME="${LATRY_VERSION_NAME}"
        LATRY_SERVICE_BUILD
    )

    set_property(TARGET applatry APPEND PROPERTY
        QT_ANDROID_EXTRA_LIBS
        $<TARGET_FILE:latryservice>
    )
    add_dependencies(applatry latryservice)

    set_target_properties(applatry PROPERTIES
        # This tells Qt where to find your AndroidManifest.xml, build.gradle, etc.
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"

        # This is the CORRECT property for the Application ID.
        # Use the full, unique name required by the Play Store.
        QT_ANDROID_PACKAGE_NAME "yo6say.latry"

        # Set the app icon from your Android resources
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"

        QT_ANDROID_APP_NAME "Latry"

        QT_ANDROID_VERSION_NAME "${LATRY_VERSION_NAME}"
        QT_ANDROID_VERSION_CODE "${LATRY_VERSION_CODE}"
    )

    # Add OpenSSL libs to APK
    add_android_openssl_libraries(applatry)
else()
    target_link_libraries(applatry PRIVATE ${OPUS_LIBRARY})
endif()

target_compile_definitions(applatry PRIVATE
    LATRY_VERSION_NAME="${LATRY_VERSION_NAME}"
)

# --- Configure Final Target Properties ---
set_target_properties(applatry PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# --- Installation Rules (Optional) ---
include(GNUInstallDirs)
install(TARGETS applatry
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING AND NOT ANDROID)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test Qml QuickTest)
    add_subdirectory(tests)

    add_custom_target(latry_live_reflector_tests
        COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/run_live_reflector_tests.sh ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
        COMMENT "Running live reflector integration tests"
    )

    find_program(JAVA_EXECUTABLE java)
    find_program(JAVAC_EXECUTABLE javac)
    find_program(ADB_EXECUTABLE adb)
    if(JAVA_EXECUTABLE AND JAVAC_EXECUTABLE)
        add_custom_target(latry_android_java_tests
            COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/run_android_java_route_tests.sh ${CMAKE_SOURCE_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM
            COMMENT "Running Android Java route-policy tests"
        )

        add_test(
            NAME latry_android_java_route_tests
            COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/run_android_java_route_tests.sh ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(latry_android_java_route_tests PROPERTIES LABELS "android-jvm")
    else()
        message(STATUS "java/javac not found: Android Java route tests disabled")
    endif()

    if(ADB_EXECUTABLE)
        add_custom_target(latry_android_device_suite
            COMMAND /bin/sh -c "exec /bin/sh '${CMAKE_SOURCE_DIR}/scripts/run_android_device_suite.sh' \"\${LATRY_ANDROID_BUILD_DIR:-}\" \"\${LATRY_ANDROID_DEVICE_SERIAL:-}\""
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM
            COMMENT "Running Android device suite with isolated instrumentation classes"
        )

        add_custom_target(latry_android_service_smoke_tests
            COMMAND /bin/sh -c "exec /bin/sh '${CMAKE_SOURCE_DIR}/scripts/run_android_service_smoke_tests.sh' \"\${LATRY_ANDROID_BUILD_DIR:-}\" \"\${LATRY_ANDROID_DEVICE_SERIAL:-}\""
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM
            COMMENT "Running Android foreground-service smoke tests on a connected device"
        )
    else()
        message(STATUS "adb not found: Android device smoke tests disabled")
    endif()
endif()

find_program(JQ_EXECUTABLE jq)
if(JQ_EXECUTABLE)
    add_custom_target(latry_deadcode
        COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/run_deadcode_scan.sh ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
        COMMENT "Running compiler-backed dead-code warning scan on first-party sources"
    )

    if(BUILD_TESTING)
        add_test(
            NAME latry_deadcode_scan
            COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/run_deadcode_scan.sh ${CMAKE_BINARY_DIR}
        )
        set_tests_properties(latry_deadcode_scan PROPERTIES LABELS "static-analysis")
    endif()
else()
    message(STATUS "jq not found: dead-code scan target disabled")
endif()
