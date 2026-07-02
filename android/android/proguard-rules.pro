# Preserve line/source metadata so retraced stacks stay useful in Sentry.
-keepattributes SourceFile,LineNumberTable,InnerClasses,EnclosingMethod,Signature,*Annotation*

# Keep R8 in shrinking/obfuscation mode to emit mapping.txt, but avoid optimizer
# passes until the Qt/JNI release path has been validated under minification.
-dontoptimize

# Qt's Android Java runtime is used from native Qt code and reflective lookups.
-keep class org.qtproject.qt.android.** { *; }
-keep class org.qtproject.qt.android.bindings.** { *; }

# Android framework entry points declared in the manifest.
-keep class yo6say.latry.LatryApplication { *; }
-keep class yo6say.latry.LatryActivity { *; }
-keep class yo6say.latry.QtLoaderConflictActivity { *; }
-keep class yo6say.latry.VoipBackgroundService { *; }
-keep class yo6say.latry.PTTButtonBroadcastReceiver { *; }
-keep class yo6say.latry.NotificationActionReceiver { *; }

# Classes reached from Qt/C++ through hard-coded JNI class/method names.
-keep class yo6say.latry.LatryAudioRouteManager { *; }
-keep class yo6say.latry.LatryAudioRecordInput { *; }
-keep class yo6say.latry.LatryAudioTrackPlayer { *; }
-keep class yo6say.latry.LatryTranscriptionManager { *; }
-keep class yo6say.latry.ConnectionProfileStore { *; }

# Native registration in JNI_OnLoad depends on exact native method names.
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}
