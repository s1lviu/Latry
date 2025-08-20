# Latry - VoIP Communication App for Amateur Radio Operators

[![Website](https://img.shields.io/badge/Website-latry.app-blue)](https://latry.app)
[![Version](https://img.shields.io/badge/Version-0.0.14-green)]()
[![Qt](https://img.shields.io/badge/Qt-6.9-41CD52)](https://www.qt.io/)
[![License](https://img.shields.io/badge/License-GPLv3-blue)](https://www.gnu.org/licenses/gpl-3.0)

[![App Store](https://img.shields.io/badge/App_Store-Download-0D96F6?logo=app-store&logoColor=white)](https://apps.apple.com/us/app/latry-a-tiny-svxlink-client/id6751135709)
[![Google Play](https://img.shields.io/badge/Google_Play-Download-414141?logo=google-play&logoColor=white)](https://play.google.com/store/apps/details?id=yo6say.latry)

Latry is a mobile SvxLink client for amateur radio enthusiasts, enabling digital voice communication across Android and iOS platforms. Built with Qt 6.9, it provides seamless connectivity to SvxReflector servers and HAM TETRA networks with low-latency audio processing.

**Website:** https://latry.app

> **Note:** Latry is not officially affiliated with the SvxLink project and is intended for hobbyist use. Users should ensure compliance with local amateur radio regulations.

## 🚀 Features

- **SvxReflector & HAM TETRA Networks** - Seamless connectivity to reflector servers
- **Low-Latency Audio** - Opus codec with integrated jitter buffer for clear communication
- **Simple Tap-to-Talk Interface** - Traditional radio-style PTT operation
- **Talkgroup Support** - Real-time transmission visibility and talkgroup management
- **Secure Authentication** - Callsign and key pairing for server access
- **Cross-Platform Support** - Native apps for iOS and Android
- **Background Operation** - Maintains connection when app is minimized
- **Lightweight Design** - Focused, efficient client (not a full SvxLink node replacement)
- **No Advertisements** - Clean, distraction-free interface
- **Dark Mode Support** - Adaptive UI that follows system theme

## 📱 Platform-Specific Features

### iOS
- **Native iOS VoIP** - CallKit integration for system-level VoIP support
- **Background Audio** - Continues operation when app is backgrounded
- **iOS 13.0+** - Optimized for modern iOS devices
- **App Store Ready** - Configured for TestFlight and App Store distribution

### Android
- **Foreground Service** - Reliable background operation with notifications
- **Battery Optimization Bypass** - Automatic handling of Android power management
- **Wake Lock Management** - Prevents device sleep during active communication
- **Notification Controls** - Quick access to PTT from notification shade

## 🏗️ Project Structure

The project uses platform-specific folders due to historical branching differences:

```
Latry/
├── android/           # Android-specific build (Qt 6.9+)
│   ├── android/       # Android manifest and resources
│   ├── CMakeLists.txt # Android build configuration
│   └── *.cpp/h        # Shared C++ source files
│
├── iOS/               # iOS-specific build (Qt 6.9+)
│   ├── ios/           # iOS-specific files and resources
│   ├── CMakeLists.txt # iOS build configuration with advanced features
│   └── *.cpp/h        # Shared C++ source files + iOS extensions
│
└── README.md          # This file
```

> **Note:** At some point there were differences between the Android and iOS branches, and it took too long to synchronize and condition the functionalities depending on the platform. The iOS branch is slightly ahead in terms of code changes.

## 🛠️ Prerequisites

### System Requirements
#### iOS
- **iOS 13.0** or newer
- **iPhone/iPad** with microphone and WiFi

#### Android  
- **Android 9.0 (API 28)** or newer
- **Device** with microphone and WiFi

### Development Requirements
#### Common
- **CMake 3.16+**
- **C++ Compiler** with C++17 support
- **Opus Audio Codec Library**

#### iOS Development
- **macOS** with Xcode 14.0+
- **Qt 6.9+** with iOS support
- **iOS Developer Account** (for device deployment)
- **Apple Developer Team ID** (for code signing)

#### Android Development
- **Qt 6.9+** with Android support
- **Android SDK** and **NDK**
- **Java 8** or higher

## 🔧 Building the Project

### iOS Build

1. **Setup Environment**
```bash
export QT_ROOT="/opt/homebrew/Qt/6.9.0"  # Adjust path as needed
export PATH="$QT_ROOT/ios/bin:$PATH"
export CMAKE_PREFIX_PATH="$QT_ROOT/ios"
```

2. **Configure Build**
```bash
cd iOS
mkdir build-ios
cd build-ios

cmake .. \
  -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$QT_ROOT/ios" \
  -DCMAKE_TOOLCHAIN_FILE="$QT_ROOT/ios/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DIOS_DEVELOPMENT_TEAM="YOUR_TEAM_ID" \
  -DQT_FEATURE_debug_and_release=ON \
  -DIOS=TRUE
```

3. **Build with Xcode**
```bash
# Open in Xcode
open Latry.xcodeproj

# Or build from command line
xcodebuild -project Latry.xcodeproj \
  -scheme Latry \
  -configuration Release \
  -destination "generic/platform=iOS"
```

### Android Build

1. **Setup Environment**
```bash
export ANDROID_SDK_ROOT="/path/to/android-sdk"
export ANDROID_NDK_ROOT="/path/to/android-ndk"
export QT_ROOT="/path/to/Qt/6.9.x"
```

2. **Configure Build**
```bash
cd android
mkdir build-android
cd build-android

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE="$QT_ROOT/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_ANDROID_ABIS="arm64-v8a" \
  -DANDROID_ABI=arm64-v8a \
  -DCMAKE_BUILD_TYPE=Release
```

3. **Build APK**
```bash
cmake --build . --parallel
```

## 📦 Dependencies

### Core Dependencies
- **Qt 6.x** - Application framework
  - QtQuick - UI framework
  - QtNetwork - Network communication
  - QtMultimedia - Audio processing
- **Opus Codec** - Audio compression
- **OpenSSL** - Secure communications (Android)

### Platform-Specific
#### iOS
- **iOS Frameworks:**
  - Foundation, UIKit
  - AVFoundation, AudioToolbox
  - CallKit, PushKit
  - UserNotifications
  - Network, VideoToolbox

#### Android
- **Android Permissions:**
  - INTERNET, RECORD_AUDIO
  - WAKE_LOCK, FOREGROUND_SERVICE
  - POST_NOTIFICATIONS
  - REQUEST_IGNORE_BATTERY_OPTIMIZATIONS

## 🎯 HAM Radio Integration

Latry is specifically designed for amateur radio operators with:

- **SvxLink Protocol Support** - Compatible with SvxLink reflector networks
- **Talkgroup Management** - Connect to specific amateur radio talkgroups
- **Authentication** - Secure key-based authentication for reflector access
- **Callsign Integration** - Proper callsign identification and display
- **PTT Operation** - Traditional push-to-talk radio operation

## 🔧 Configuration

### Connection Settings
- **Host** - Reflector server address (e.g., `reflector.145500.xyz`)
- **Port** - Connection port (typically `5300`)
- **Callsign** - Your amateur radio callsign
- **Authentication Key** - Provided by reflector administrator
- **Talkgroup** - Target talkgroup number

### iOS Specific Setup
```bash
# Set your Apple Developer Team ID
-DIOS_DEVELOPMENT_TEAM="1A2BC3D4E5"

# Configure bundle identifier
-DIOS_BUNDLE_IDENTIFIER="yo6say.latry"
```

## 🐛 Known Issues & Troubleshooting

### iOS
- **Archive Issues**: Enable `QT_USE_RISKY_DSYM_ARCHIVING_WORKAROUND` for TestFlight builds
- **FFmpeg Frameworks**: Debug binaries are automatically cleaned from frameworks
- **Code Signing**: Ensure valid Team ID and automatic signing is enabled

### Android
- **Battery Optimization**: App guides users through battery optimization settings
- **Background Service**: Uses foreground service for reliable VoIP operation
- **OpenSSL**: KDAB OpenSSL integration for secure communications

## 🤝 Contributing

This project is published as open source because many people want various functionalities, and implementing all requests takes considerable time. The community is free to add and modify features as needed.

**Pull Requests Welcome!** I am open to pull requests and will continue to host the application on Google Play and the App Store.

When contributing:

1. Follow existing code style and conventions
2. Test on both iOS and Android platforms when possible
3. Update documentation for new features
4. Ensure compatibility with SvxLink reflector protocol
5. Consider the platform-specific folder structure

## 📄 License

This project is licensed under the **GNU General Public License v3.0** (GPLv3).

You are free to:
- Use, modify, and distribute this software
- Create derivative works
- Use it for commercial purposes

Under the following conditions:
- Provide source code of your modifications
- Use the same GPLv3 license for derivative works
- Include license and copyright notices

See the [LICENSE](LICENSE) file for full details.

## 👨‍💻 Author

**Silviu YO6SAY**  
Amateur Radio Operator & CTO

## 🔗 Links

- **Website:** [latry.app](https://latry.app)
- **iOS App Store:** [Download Latry](https://apps.apple.com/us/app/latry-a-tiny-svxlink-client/id6751135709)
- **Google Play Store:** [Download Latry](https://play.google.com/store/apps/details?id=yo6say.latry)

---

*Built with ❤️ for the Amateur Radio Community*