cmake_minimum_required(VERSION 3.16)

project(latry VERSION 0.1 LANGUAGES CXX)

# Add Objective-C++ only for iOS/macOS builds
if(IOS OR APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---- iOS signing/config knobs (replace to match your Apple account) ----
# Use a reverse-DNS bundle id that matches your App ID in the Apple Developer portal.
set(IOS_BUNDLE_IDENTIFIER "yo6say.latry" CACHE STRING "iOS bundle identifier for automatic signing")

# Your 10-character Apple Developer Team ID, e.g. 1A2BC3D4E5
set(IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple Developer Team ID for automatic signing")

# iOS-specific symbol visibility settings (recommended by Qt documentation)
if(IOS)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    add_definitions(-DQ_OS_IOS)

    # Build device slice only when archiving; helps avoid fat binaries sneaking into archives
    # You can override at configure time if needed.
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architectures for iOS build" FORCE)

    # Require a Team ID for automatic signing to work reliably in CI/Archive
    if(IOS_DEVELOPMENT_TEAM STREQUAL "")
        message(FATAL_ERROR "IOS_DEVELOPMENT_TEAM is empty. Set -DIOS_DEVELOPMENT_TEAM=<YOUR_TEAM_ID> for automatic signing.")
    endif()
endif()

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

    add_subdirectory(3rdparty/opus)
    include_directories("${opus_SOURCE_DIR}/include")

elseif(IOS)
    # --- iOS ---
    message(STATUS "iOS build detected. Building Opus from 3rdparty/opus with iOS optimizations")
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "Disable Opus tests" FORCE)
    set(OPUS_BUILD_DEMO OFF CACHE BOOL "Disable Opus demo" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Disable Opus programs" FORCE)

    # Prefer fixed-point on mobile; keep ARM intrinsics enabled
    set(OPUS_FIXED_POINT ON CACHE BOOL "Use fixed-point arithmetic on iOS" FORCE)
    set(OPUS_DISABLE_INTRINSICS OFF CACHE BOOL "Enable ARM optimizations on iOS" FORCE)

    add_subdirectory(3rdparty/opus)
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
endif()

# --- Define the Application Target ---
set(COMMON_SOURCES
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

# Add iOS-specific sources
if(IOS)
    list(APPEND COMMON_SOURCES
        ios/IOSAudioManager.mm
        ios/IOSVoIPHandler.cpp
    )
    
    # Qt 6.9 official workaround for dSYM bundle inclusion in xcarchive
    # Only enable when creating archives for distribution/TestFlight
    # Trade-offs: breaks some CMake generator expressions & runtime output directory
    set(QT_USE_RISKY_DSYM_ARCHIVING_WORKAROUND ON)
endif()

qt_add_executable(Latry ${COMMON_SOURCES})

# Add iOS Asset Catalog after the executable is created
if(IOS)
    set(asset_catalog_path "ios/Assets.xcassets")
    target_sources(Latry PRIVATE "${asset_catalog_path}")
    set_source_files_properties(
        ${asset_catalog_path}
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# --- Configure the Target's Links ---
target_link_libraries(Latry PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
)

if(ANDROID)
    if(TARGET Qt6::AndroidExtras)
        target_link_libraries(Latry PRIVATE Qt6::AndroidExtras)
    endif()
    target_link_libraries(Latry PRIVATE Qt6::CorePrivate opus)

    set_target_properties(Latry PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "yo6say.latry"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
        QT_ANDROID_APP_NAME "Latry"
    )

    # Add OpenSSL libs to APK
    add_android_openssl_libraries(Latry)

elseif(IOS)
    # iOS-specific configuration
    target_link_libraries(Latry PRIVATE opus)

    # Link iOS frameworks required for VoIP functionality
    target_link_libraries(Latry PRIVATE
        "-framework Foundation"
        "-framework UIKit"
        "-framework AVFoundation"
        "-framework AudioToolbox"
        "-framework CallKit"
        "-framework PushKit"
        "-framework UserNotifications"
        "-framework Network"
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
    )

    # Use Qt's helper to embed the correct FFmpeg iOS frameworks and sign them on copy
    qt_add_ios_ffmpeg_libraries(Latry)
    message(STATUS "Added FFmpeg libraries for iOS using Qt's official function")

    # Critical: Remove debug binaries from FFmpeg frameworks (required for App Store)
    # Qt's FFmpeg xcframeworks include both release and debug binaries
    # App Store validation rejects apps with *_debug binaries in frameworks
    add_custom_command(TARGET Latry POST_BUILD
        COMMAND /bin/sh -c 'find "$BUILT_PRODUCTS_DIR/$CONTENTS_FOLDER_PATH/Frameworks" -maxdepth 2 -name "*_debug" -delete || true'
        COMMAND /bin/sh -c 'echo "Debug binary cleanup completed for frameworks"'
        COMMENT "Remove stray *_debug binaries from embedded frameworks"
    )

    # iOS deployment target
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")

    # --- iOS target properties focused on Automatic Signing & clean archives ---
    set_target_properties(Latry PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "${IOS_BUNDLE_IDENTIFIER}"
        MACOSX_BUNDLE_BUNDLE_NAME "Latry"
        MACOSX_BUNDLE_DISPLAY_NAME "Latry VoIP"
        MACOSX_BUNDLE_INFO_STRING "VoIP communication app for radio operators"
        MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION "22"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist"

        # Xcode-specific properties for iOS development / distribution
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${IOS_BUNDLE_IDENTIFIER}"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"

        # Ensure device build (archive uses iphoneos; simulator slices must not leak in)
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "13.0"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"

        # Bitcode is deprecated; keep off to avoid toolchain surprises
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"

        # ARC for ObjC++
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"

        # Optimization & stripping for Release
        XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT[variant=Release] "YES"
        XCODE_ATTRIBUTE_COPY_PHASE_STRIP[variant=Release] "YES"
        XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING[variant=Release] "YES"
        XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL[variant=Release] "s"
        XCODE_ATTRIBUTE_SWIFT_OPTIMIZATION_LEVEL[variant=Release] "-O"

        # Correct dSYM generation for Release builds (for App Store Connect symbol upload)
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release] "dwarf-with-dsym"

        # App Icon configuration - point to the asset catalog
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"

        # Do not skip install for the app target in archives
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    )

else()
    # Desktop build
    target_link_libraries(Latry PRIVATE ${OPUS_LIBRARY})
endif()

# --- Configure Final Target Properties ---
set_target_properties(Latry PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# --- Installation Rules (Optional) ---
include(GNUInstallDirs)
install(TARGETS Latry
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
