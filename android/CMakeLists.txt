cmake_minimum_required(VERSION 3.16)

project(latry VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Find/Build All Dependencies FIRST ---

# 1. Find Qt6 - This MUST come before any qt_* commands.
find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Network Multimedia)
if(ANDROID)
    # AndroidExtras is optional in Qt 6.5+ where APIs moved to QtCore
    find_package(Qt6AndroidExtras QUIET)

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
    # No external resampler dependency is required
endif()


# --- Define the Application Target ---
qt_add_executable(applatry
    main.cpp
    ReflectorClient.cpp
    AudioEngine.cpp
    AudioJitterBuffer.cpp
    AudioStreamDevice.cpp
    OpusWrapper.cpp
    Resampler.cpp
    BatteryOptimizationHandler.cpp
    qml.qrc
)

# --- Configure the Target's Links ---
target_link_libraries(applatry PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
)

if(ANDROID)
    if(TARGET Qt6::AndroidExtras)
        target_link_libraries(applatry PRIVATE Qt6::AndroidExtras)
    endif()
    target_link_libraries(applatry PRIVATE Qt6::CorePrivate opus)

    set_target_properties(applatry PROPERTIES
        # This tells Qt where to find your AndroidManifest.xml, build.gradle, etc.
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"

        # This is the CORRECT property for the Application ID.
        # Use the full, unique name required by the Play Store.
        QT_ANDROID_PACKAGE_NAME "yo6say.latry"

        # Set the app icon from your Android resources
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"

        QT_ANDROID_APP_NAME "Latry"
    )

    # Add OpenSSL libs to APK
    add_android_openssl_libraries(applatry)
else()
    target_link_libraries(applatry PRIVATE ${OPUS_LIBRARY})
endif()


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
