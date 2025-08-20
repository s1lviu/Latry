# iOS Build Instructions for Latry VoIP App

## Prerequisites

1. **macOS with Xcode 14.0+**
2. **Qt 6.9+ with iOS support**
3. **CMake 3.16+**
4. **iOS Developer Account** (for device deployment)

## Build Steps

### 1. Configure Qt for iOS

Ensure Qt is installed with iOS support:
```bash
# Check Qt installation
qmake -version
# Should show Qt 6.9+ version

# Verify iOS kit is available
ls $QT_ROOT/6.9.0/ios/
```

### 2. Configure Build Environment

```bash
# Set environment variables
export QT_ROOT="/opt/homebrew/Qt/6.9.0"  # Adjust path as needed
export PATH="$QT_ROOT/ios/bin:$PATH"
export CMAKE_PREFIX_PATH="$QT_ROOT/ios"

# Create build directory
mkdir build-ios
cd build-ios
```

### 3. Generate Xcode Project

```bash
# Configure CMake for iOS
cmake .. \
  -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$QT_ROOT/ios" \
  -DCMAKE_TOOLCHAIN_FILE="$QT_ROOT/ios/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_FEATURE_debug_and_release=ON \
  -DIOS=TRUE
```

### 4. Build with Xcode

```bash
# Open in Xcode
open applatry.xcodeproj

# Or build from command line
xcodebuild -project applatry.xcodeproj \
  -scheme applatry \
  -configuration Release \
  -destination "generic/platform=iOS"
```