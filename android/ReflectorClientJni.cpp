/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ReflectorClient.h"
#include "AndroidAudioRouteInterop.h"

#if defined(Q_OS_ANDROID)
#include <jni.h>
#include <android/log.h>
#include <QJniEnvironment>
#include <QtCore/QJniObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QDebug>
#include <QMetaType>
#include <QPointer>
#include <QSharedPointer>
#include <QThread>
#include <QSemaphore>
#include <mutex>
#include <functional>
#include <vector>

namespace {
enum AndroidControlEvent {
    AndroidControlPttPress = 1,
    AndroidControlPttRelease = 2,
    AndroidControlMediaPlay = 3,
    AndroidControlMediaPause = 4,
    AndroidControlMediaStop = 5
};

const char* androidControlEventName(int eventType)
{
    switch (eventType) {
    case AndroidControlPttPress:
        return "PTT_PRESS";
    case AndroidControlPttRelease:
        return "PTT_RELEASE";
    case AndroidControlMediaPlay:
        return "MEDIA_PLAY";
    case AndroidControlMediaPause:
        return "MEDIA_PAUSE";
    case AndroidControlMediaStop:
        return "MEDIA_STOP";
    default:
        return "UNKNOWN";
    }
}

QString fromJString(JNIEnv *env, jstring value)
{
    if (value == nullptr) {
        return {};
    }

    const char *utfChars = env->GetStringUTFChars(value, nullptr);
    if (utfChars == nullptr) {
        return {};
    }

    const QString result = QString::fromUtf8(utfChars);
    env->ReleaseStringUTFChars(value, utfChars);
    return result;
}

QJniObject androidContext()
{
    return QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
}

bool loadSavedAndroidConnectionProfile(QString &host,
                                       int &port,
                                       QByteArray &authKey,
                                       QString &callsign,
                                       quint32 &talkgroup,
                                       QString &monitoredTalkgroups,
                                       int &tgSelectTimeoutSeconds)
{
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for loading saved reconnect profile";
        return false;
    }

    const jboolean hasProfile = QJniObject::callStaticMethod<jboolean>(
        "yo6say/latry/ConnectionProfileStore",
        "hasSavedConnectionProfile",
        "(Landroid/content/Context;)Z",
        context.object());
    if (!hasProfile) {
        return false;
    }

    const QJniObject hostObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedHost",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());
    const jint savedPort = QJniObject::callStaticMethod<jint>(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedPort",
        "(Landroid/content/Context;)I",
        context.object());
    const QJniObject authKeyObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedAuthKey",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());
    const QJniObject callsignObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedCallsign",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());
    const jint savedTalkgroup = QJniObject::callStaticMethod<jint>(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedTalkgroup",
        "(Landroid/content/Context;)I",
        context.object());
    const jint savedTgSelectTimeout = QJniObject::callStaticMethod<jint>(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedTgSelectTimeout",
        "(Landroid/content/Context;)I",
        context.object());
    const QJniObject monitoredObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/ConnectionProfileStore",
        "getSavedMonitoredTalkgroups",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());

    host = hostObject.toString().trimmed();
    port = static_cast<int>(savedPort);
    authKey = authKeyObject.toString().trimmed().toUtf8();
    callsign = callsignObject.toString().trimmed();
    talkgroup = static_cast<quint32>(savedTalkgroup);
    monitoredTalkgroups = monitoredObject.toString().trimmed();
    tgSelectTimeoutSeconds = static_cast<int>(savedTgSelectTimeout) > 0
            ? static_cast<int>(savedTgSelectTimeout)
            : 30;

    return !host.isEmpty() && port > 0 && !authKey.isEmpty() && !callsign.isEmpty();
}

using PendingReflectorAction = std::function<void(ReflectorClient*)>;

std::mutex g_pendingReflectorActionsMutex;
std::vector<PendingReflectorAction> g_pendingReflectorActions;

void enqueuePendingReflectorAction(const char *reason, PendingReflectorAction callback)
{
    {
        std::lock_guard<std::mutex> lock(g_pendingReflectorActionsMutex);
        g_pendingReflectorActions.push_back(std::move(callback));
    }
    qDebug() << reason << "- queued until Qt application is ready";
}

void dispatchReflectorActionOnQtThread(const char *reason, PendingReflectorAction callback)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        enqueuePendingReflectorAction(reason, std::move(callback));
        return;
    }

    if (QThread::currentThread() == app->thread()) {
        ReflectorClient *client = ReflectorClient::instance();
        if (!client) {
            enqueuePendingReflectorAction(reason, std::move(callback));
            return;
        }
        callback(client);
        return;
    }

    QMetaObject::invokeMethod(app, [reason, callback = std::move(callback)]() mutable {
        dispatchReflectorActionOnQtThread(reason, std::move(callback));
    }, Qt::QueuedConnection);
}
}

namespace AndroidReflectorClientJniInterop {
void drainPendingReflectorActions(ReflectorClient *client)
{
    if (!client) {
        return;
    }

    std::vector<PendingReflectorAction> pendingActions;
    {
        std::lock_guard<std::mutex> lock(g_pendingReflectorActionsMutex);
        if (g_pendingReflectorActions.empty()) {
            return;
        }
        pendingActions.swap(g_pendingReflectorActions);
    }

    for (auto &pendingAction : pendingActions) {
        pendingAction(client);
    }
}
}

bool ReflectorClient::loadSavedAndroidReconnectProfile(QString &host,
                                                       int &port,
                                                       QByteArray &authKey,
                                                       QString &callsign,
                                                       quint32 &talkgroup,
                                                       QString &monitoredTalkgroups,
                                                       int &tgSelectTimeoutSeconds)
{
    return loadSavedAndroidConnectionProfile(host, port, authKey, callsign,
                                             talkgroup, monitoredTalkgroups,
                                             tgSelectTimeoutSeconds);
}

// JNI callback declarations
extern "C" {
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusLost(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusGained(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityPaused(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityResumed(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_nativePrepareForShutdown(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyHardwarePttLearningResult(JNIEnv *, jclass, jint, jint);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_nativeNotifyAutoDetectedPttKeyCode(JNIEnv *, jclass, jint);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyAndroidControlEvent(JNIEnv *, jclass, jint);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyNetworkStateChanged(JNIEnv *, jclass,
                                                                                              jint, jint, jboolean,
                                                                                              jboolean, jint,
                                                                                              jboolean, jboolean,
                                                                                              jboolean);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryAudioRouteManager_notifyAudioRoutesChanged(JNIEnv *, jclass, jstring, jstring);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyPartialTranscription(JNIEnv *, jclass, jstring);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyFinalTranscription(JNIEnv *, jclass, jstring);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionError(JNIEnv *, jclass, jint, jstring);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionStopped(JNIEnv *, jclass);

    // JNI_OnLoad - CRITICAL for Qt 6.9 Android JNI method registration
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
        Q_UNUSED(vm)
        Q_UNUSED(reserved)

        QJniEnvironment env;
        if (!env.isValid()) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to get JNI environment");
            return JNI_ERR;
        }

        qRegisterMetaType<QStringList>("QStringList");

        const JNINativeMethod activityMethods[] = {
            {"notifyAudioFocusLost", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusLost)},
            {"notifyAudioFocusPaused", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused)},
            {"notifyAudioFocusGained", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusGained)},
            {"notifyActivityPaused", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyActivityPaused)},
            {"notifyActivityResumed", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyActivityResumed)},
            {"nativePrepareForShutdown", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_nativePrepareForShutdown)},
            {"notifyHardwarePttLearningResult", "(II)V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyHardwarePttLearningResult)},
            {"nativeNotifyAutoDetectedPttKeyCode", "(I)V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_nativeNotifyAutoDetectedPttKeyCode)}
        };

        const JNINativeMethod serviceMethods[] = {
            {"notifyServiceStarted", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted)},
            {"notifyServiceStopped", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped)},
            {"notifyCheckConnection", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection)},
            {"notifyAndroidControlEvent", "(I)V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyAndroidControlEvent)},
            {"notifyNetworkStateChanged", "(IIZZIZZZ)V",
             reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyNetworkStateChanged)}
        };

        const JNINativeMethod audioRouteMethods[] = {
            {"notifyAudioRoutesChanged", "(Ljava/lang/String;Ljava/lang/String;)V",
             reinterpret_cast<void*>(Java_yo6say_latry_LatryAudioRouteManager_notifyAudioRoutesChanged)}
        };

        const JNINativeMethod transcriptionMethods[] = {
            {"notifyPartialTranscription", "(Ljava/lang/String;)V",
             reinterpret_cast<void*>(Java_yo6say_latry_LatryTranscriptionManager_notifyPartialTranscription)},
            {"notifyFinalTranscription", "(Ljava/lang/String;)V",
             reinterpret_cast<void*>(Java_yo6say_latry_LatryTranscriptionManager_notifyFinalTranscription)},
            {"notifyTranscriptionError", "(ILjava/lang/String;)V",
             reinterpret_cast<void*>(Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionError)},
            {"notifyTranscriptionStopped", "()V",
             reinterpret_cast<void*>(Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionStopped)}
        };

        if (!env.registerNativeMethods("yo6say/latry/LatryActivity", activityMethods, 8)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register LatryActivity native methods");
            return JNI_ERR;
        }

        if (!env.registerNativeMethods("yo6say/latry/VoipBackgroundService", serviceMethods, 5)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register VoipBackgroundService native methods");
            return JNI_ERR;
        }

        if (!env.registerNativeMethods("yo6say/latry/LatryAudioRouteManager", audioRouteMethods, 1)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register LatryAudioRouteManager native methods");
            return JNI_ERR;
        }

        if (!env.registerNativeMethods("yo6say/latry/LatryTranscriptionManager",
                                       transcriptionMethods, 4)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register LatryTranscriptionManager native methods");
            return JNI_ERR;
        }

        return JNI_VERSION_1_6;
    }
}

// --- Static JNI notification methods ---

void ReflectorClient::notifyAudioFocusLost()
{
    qDebug() << "JNI: Audio focus lost permanently";
    dispatchReflectorActionOnQtThread("Audio focus lost", [](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client]() {
                emit client->audioFocusLost();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyAudioFocusPaused()
{
    qDebug() << "JNI: Audio focus paused temporarily";
    dispatchReflectorActionOnQtThread("Audio focus paused", [](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client]() {
                emit client->audioFocusPaused();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyAudioFocusGained()
{
    qDebug() << "JNI: Audio focus gained";
    dispatchReflectorActionOnQtThread("Audio focus gained", [](ReflectorClient *client) {
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        QMetaObject::invokeMethod(
            client,
            [client]() {
                emit client->audioFocusGained();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyActivityPaused()
{
    qDebug() << "JNI: Activity paused";
    dispatchReflectorActionOnQtThread("Activity paused", [](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client]() {
                emit client->activityPaused();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyActivityResumed()
{
    qDebug() << "JNI: Activity resumed";
    dispatchReflectorActionOnQtThread("Activity resumed", [](ReflectorClient *client) {
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        QMetaObject::invokeMethod(
            client,
            [client]() {
                emit client->activityResumed();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyHardwarePttLearningResult(int result, int keyCode)
{
    qDebug() << "JNI: Hardware PTT learning result=" << result << "keyCode=" << keyCode;
    dispatchReflectorActionOnQtThread("Hardware PTT learning result",
                                      [result, keyCode](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, result, keyCode]() {
                client->m_hardwarePttLearningActive = false;
                client->m_hardwarePttLearningResult = result;

                // Sync the learned key from SharedPreferences after learning
                if (result == 1 && keyCode > 0) { // RESULT_KEY_CAPTURED
                    client->m_learnedHardwarePttKeyCode = keyCode;
                    emit client->hardwarePttSettingsChanged();
                }

                emit client->hardwarePttLearningActiveChanged();
                emit client->hardwarePttLearningResultChanged();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyAutoDetectedPttKeyCode(int keyCode)
{
    const int normalizedKeyCode = keyCode > 0 ? keyCode : -1;
    qDebug() << "JNI: Auto-detected hardware PTT keyCode=" << normalizedKeyCode;
    if (normalizedKeyCode <= 0) {
        return;
    }

    dispatchReflectorActionOnQtThread("Auto-detected hardware PTT key",
                                      [normalizedKeyCode](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, normalizedKeyCode]() {
                if (client->m_learnedHardwarePttKeyCode == normalizedKeyCode) {
                    return;
                }

                client->m_learnedHardwarePttKeyCode = normalizedKeyCode;
                emit client->hardwarePttSettingsChanged();
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyAndroidControlEvent(int eventType)
{
    qDebug() << "JNI: Android control event" << androidControlEventName(eventType);
    dispatchReflectorActionOnQtThread("Android control event", [eventType](ReflectorClient *client) {
        QMetaObject::invokeMethod(client, "handleAndroidControlEvent",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, eventType));
    });
}

void ReflectorClient::notifyAndroidNetworkStateChanged(int generation,
                                                       int reason,
                                                       bool hasDefaultNetwork,
                                                       bool validated,
                                                       int transport,
                                                       bool metered,
                                                       bool captivePortal,
                                                       bool routeChanged)
{
    qDebug() << "JNI: Android network state changed"
             << "generation=" << generation
             << "reason=" << reason
             << "defaultNetwork=" << hasDefaultNetwork
             << "validated=" << validated
             << "transport=" << transport
             << "metered=" << metered
             << "captivePortal=" << captivePortal
             << "routeChanged=" << routeChanged;
    dispatchReflectorActionOnQtThread("Android network state changed",
                                      [generation, reason, hasDefaultNetwork, validated,
                                       transport, metered, captivePortal, routeChanged](ReflectorClient *client) {
        QMetaObject::invokeMethod(client, "handleAndroidNetworkStateChanged",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, generation),
                                  Q_ARG(int, reason),
                                  Q_ARG(bool, hasDefaultNetwork),
                                  Q_ARG(bool, validated),
                                  Q_ARG(int, transport),
                                  Q_ARG(bool, metered),
                                  Q_ARG(bool, captivePortal),
                                  Q_ARG(bool, routeChanged));
    });
}

void ReflectorClient::notifyAndroidAudioRoutesChanged(const QString &currentRoute,
                                                      const QStringList &availableRoutes)
{
    qDebug() << "JNI: Android audio routes changed current=" << currentRoute
             << "available=" << availableRoutes;
    dispatchReflectorActionOnQtThread("Android audio routes changed",
                                      [currentRoute, availableRoutes](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, currentRoute, availableRoutes]() {
                client->handleAndroidAudioRoutesChanged(currentRoute, availableRoutes);
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyPartialTranscription(const QString &text)
{
    dispatchReflectorActionOnQtThread("Transcription partial", [text](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, text]() {
                client->handlePartialTranscription(text);
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyFinalTranscription(const QString &text)
{
    dispatchReflectorActionOnQtThread("Transcription final", [text](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, text]() {
                client->handleFinalTranscription(text);
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyTranscriptionError(int errorCode, const QString &message)
{
    qWarning() << "JNI: live transcription error" << errorCode << message;
    dispatchReflectorActionOnQtThread("Transcription error",
                                      [errorCode, message](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client, errorCode, message]() {
                client->handleTranscriptionError(errorCode, message);
            },
            Qt::QueuedConnection);
    });
}

void ReflectorClient::notifyTranscriptionStopped()
{
    dispatchReflectorActionOnQtThread("Transcription stopped", [](ReflectorClient *client) {
        QMetaObject::invokeMethod(
            client,
            [client]() {
                client->m_transcriptionSessionActive = false;
                client->m_transcriptionPendingText.clear();
                client->stopTranscriptionSession(false);
                client->updateTranscriptionDisplay();
            },
            Qt::QueuedConnection);
    });
}

// --- JNI callback implementations ---

extern "C" {
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusLost(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusLost();
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusPaused();
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusGained(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusGained();
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityPaused(JNIEnv *, jclass) {
        ReflectorClient::notifyActivityPaused();
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityResumed(JNIEnv *, jclass) {
        ReflectorClient::notifyActivityResumed();
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_nativePrepareForShutdown(JNIEnv *, jclass) {
        // Called from LatryActivity.onDestroy() BEFORE super.onDestroy() invokes
        // terminateQtNativeApplication(). The Android main thread is still free here,
        // so we can safely dispatch to the Qt thread and wait for completion.
        ReflectorClient *client = ReflectorClient::instance();
        if (!client)
            return;

        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return;

        if (QThread::currentThread() == app->thread()) {
            client->prepareForShutdown();
            return;
        }

        // Post to the Qt thread and wait with a bounded timeout.
        // Queued delivery can outlive this JNI frame, so the semaphore storage
        // must outlive the wait timeout as well.
        auto semaphore = QSharedPointer<QSemaphore>::create();
        QPointer<ReflectorClient> guardedClient(client);
        const bool queued = QMetaObject::invokeMethod(
            app,
            [guardedClient, semaphore]() {
                if (guardedClient) {
                    guardedClient->prepareForShutdown();
                }
                semaphore->release();
            },
            Qt::QueuedConnection);
        if (!queued) {
            __android_log_print(ANDROID_LOG_WARN, "ReflectorClient",
                "nativePrepareForShutdown: failed to queue shutdown preparation on Qt thread");
            return;
        }

        if (!semaphore->tryAcquire(1, 2000)) {
            __android_log_print(ANDROID_LOG_WARN, "ReflectorClient",
                "nativePrepareForShutdown: timed out waiting for Qt thread (2 s)");
        }
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyHardwarePttLearningResult(JNIEnv *, jclass, jint result, jint keyCode) {
        ReflectorClient::notifyHardwarePttLearningResult(static_cast<int>(result), static_cast<int>(keyCode));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_nativeNotifyAutoDetectedPttKeyCode(JNIEnv *, jclass, jint keyCode) {
        ReflectorClient::notifyAutoDetectedPttKeyCode(static_cast<int>(keyCode));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted(JNIEnv *, jclass) {
        qDebug() << "JNI: VoIP service started";
        dispatchReflectorActionOnQtThread("VoIP service start", [](ReflectorClient *) {
        });
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped(JNIEnv *, jclass) {
        qDebug() << "JNI: VoIP service stopped";
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection(JNIEnv *, jclass) {
        dispatchReflectorActionOnQtThread("VoIP service connection check", [](ReflectorClient *client) {
            QMetaObject::invokeMethod(client, "checkAndReconnect", Qt::QueuedConnection);
        });
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyAndroidControlEvent(JNIEnv *, jclass, jint eventType) {
        ReflectorClient::notifyAndroidControlEvent(eventType);
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyNetworkStateChanged(
            JNIEnv *, jclass, jint generation, jint reason, jboolean hasDefaultNetwork,
            jboolean validated, jint transport, jboolean metered,
            jboolean captivePortal, jboolean routeChanged) {
        ReflectorClient::notifyAndroidNetworkStateChanged(
                static_cast<int>(generation),
                static_cast<int>(reason),
                hasDefaultNetwork == JNI_TRUE,
                validated == JNI_TRUE,
                static_cast<int>(transport),
                metered == JNI_TRUE,
                captivePortal == JNI_TRUE,
                routeChanged == JNI_TRUE);
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryAudioRouteManager_notifyAudioRoutesChanged(JNIEnv *env, jclass, jstring currentRoute, jstring availableRoutesJson) {
        ReflectorClient::notifyAndroidAudioRoutesChanged(
            AndroidAudioRouteInterop::normalizeRouteId(fromJString(env, currentRoute)),
            AndroidAudioRouteInterop::parseRoutesJson(fromJString(env, availableRoutesJson)));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyPartialTranscription(JNIEnv *env, jclass, jstring text) {
        ReflectorClient::notifyPartialTranscription(fromJString(env, text));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyFinalTranscription(JNIEnv *env, jclass, jstring text) {
        ReflectorClient::notifyFinalTranscription(fromJString(env, text));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionError(JNIEnv *env, jclass, jint errorCode, jstring message) {
        ReflectorClient::notifyTranscriptionError(static_cast<int>(errorCode),
                                                  fromJString(env, message));
    }

    JNIEXPORT void JNICALL Java_yo6say_latry_LatryTranscriptionManager_notifyTranscriptionStopped(JNIEnv *, jclass) {
        ReflectorClient::notifyTranscriptionStopped();
    }
}

void ReflectorClient::handleAndroidControlEvent(int eventType)
{
    qDebug() << "Handling Android control event:" << androidControlEventName(eventType);

    switch (eventType) {
    case AndroidControlPttPress:
        if (m_state != Connected || !m_audioReady) {
            m_resumeAndroidPttAfterReconnect = true;
        }
        if (m_pttReleasePending || !m_pttActive) {
            pttPressed();
        }
        break;
    case AndroidControlPttRelease:
        m_resumeAndroidPttAfterReconnect = false;
        pttReleased();
        break;
    case AndroidControlMediaPlay:
        if (m_state == Disconnected) {
            QString host = m_host;
            int port = m_port;
            QByteArray authKey = m_authKey;
            QString callsign = m_callsign;
            quint32 talkgroup = m_talkgroup;
            QString monitoredTalkgroups = m_monitoredTalkgroupsSpec;
            int tgSelectTimeoutSeconds = m_tgSelectTimeoutSeconds;

            if (host.isEmpty() || port <= 0 || authKey.isEmpty() || callsign.isEmpty()) {
                loadSavedAndroidReconnectProfile(host, port, authKey, callsign,
                                                 talkgroup, monitoredTalkgroups,
                                                 tgSelectTimeoutSeconds);
            }

            if (!host.isEmpty() && port > 0 && !authKey.isEmpty() && !callsign.isEmpty()) {
                connectToServer(host, port, QString::fromUtf8(authKey), callsign,
                                talkgroup, monitoredTalkgroups,
                                tgSelectTimeoutSeconds);
                break;
            }
        }

        qWarning() << "Ignoring MEDIA_PLAY event because reconnect context is incomplete or already active"
                   << "state=" << m_state
                   << "hostEmpty=" << m_host.isEmpty()
                   << "port=" << m_port
                   << "authEmpty=" << m_authKey.isEmpty()
                   << "callsignEmpty=" << m_callsign.isEmpty();
        break;
    case AndroidControlMediaPause:
    case AndroidControlMediaStop:
        m_resumeAndroidPttAfterReconnect = false;
        if (m_pttActive) {
            forcePttRelease();
        }
        if (m_state != Disconnected) {
            disconnectFromServer();
        }
        break;
    default:
        qWarning() << "Unhandled Android control event:" << eventType;
        break;
    }
}

// --- Android service management ---

void ReflectorClient::acquireWakeLock()
{
    qDebug() << "Acquiring wake lock for background VoIP";
    QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "acquireWakeLock", "()V");
}

void ReflectorClient::releaseWakeLock()
{
    qDebug() << "Releasing wake lock";
    QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "releaseWakeLock", "()V");
}

void ReflectorClient::ensureVoipService()
{
    qDebug() << "Ensuring Android Auto controller service is running";
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for ensuring VoIP service";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "yo6say/latry/VoipBackgroundService",
        "ensureControllerService",
        "(Landroid/content/Context;)V",
        context.object());
}

void ReflectorClient::startVoipService(bool monitorConnection)
{
    qDebug() << "Starting VoIP background service monitorConnection=" << monitorConnection;
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for starting VoIP service";
        return;
    }

    QJniObject hostStr = QJniObject::fromString(m_host);
    QJniObject callsignStr = QJniObject::fromString(m_callsign);

    QJniObject::callStaticMethod<void>("yo6say/latry/VoipBackgroundService",
        "startVoipService",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;IZ)V",
        context.object(),
        hostStr.object(),
        m_port,
        callsignStr.object(),
        m_talkgroup,
        static_cast<jboolean>(monitorConnection)
    );
}

void ReflectorClient::stopVoipService()
{
    qDebug() << "Stopping VoIP background service";
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for stopping VoIP service";
        return;
    }

    QJniObject::callStaticMethod<void>("yo6say/latry/VoipBackgroundService",
        "stopVoipService",
        "(Landroid/content/Context;)V",
        context.object()
    );
}

void ReflectorClient::setServiceConnectionMonitoring(bool enabled)
{
    const QJniObject serviceInstance = QJniObject::callStaticObjectMethod(
        "yo6say/latry/VoipBackgroundService",
        "getInstance",
        "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        serviceInstance.callMethod<void>(
            "setConnectionMonitoringEnabled",
            "(Z)V",
            static_cast<jboolean>(enabled));
        return;
    }

    if (!enabled) {
        ensureVoipService();
    }
}

void ReflectorClient::initializeAndroidAudioRouting()
{
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: Failed to get Android context for audio route monitoring";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "yo6say/latry/LatryAudioRouteManager",
        "startMonitoring",
        "(Landroid/content/Context;)V",
        context.object());

    const QJniObject currentRouteObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/LatryAudioRouteManager",
        "getCurrentRoute",
        "()Ljava/lang/String;");
    const QJniObject availableRoutesObject = QJniObject::callStaticObjectMethod(
        "yo6say/latry/LatryAudioRouteManager",
        "getAvailableRoutesJson",
        "()Ljava/lang/String;");

    handleAndroidAudioRoutesChanged(
        AndroidAudioRouteInterop::normalizeRouteId(currentRouteObject.toString()),
        AndroidAudioRouteInterop::parseRoutesJson(availableRoutesObject.toString()));
}

void ReflectorClient::stopAndroidAudioRouting()
{
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/LatryAudioRouteManager",
        "stopMonitoring",
        "()V");
}

void ReflectorClient::updateServiceConnectionStatus(const QString& status, bool connected)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        QJniObject statusStr = QJniObject::fromString(status);
        serviceInstance.callMethod<void>("updateConnectionStatus", "(Ljava/lang/String;Z)V",
            statusStr.object(), connected);
    }
}

void ReflectorClient::updateServiceCurrentTalker(const QString& talker)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        QJniObject talkerStr = QJniObject::fromString(talker);
        serviceInstance.callMethod<void>("updateCurrentTalker", "(Ljava/lang/String;)V",
            talkerStr.object());
    }
}

void ReflectorClient::updateServiceSelectedTalkgroup(quint32 talkgroup)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        serviceInstance.callMethod<void>("updateTalkgroup", "(I)V", static_cast<jint>(talkgroup));
    }
}

void ReflectorClient::updateServiceReceiveState(bool receiving, const QString& talker)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        QJniObject talkerStr = QJniObject::fromString(talker);
        serviceInstance.callMethod<void>("updateReceiveState", "(ZLjava/lang/String;)V",
            static_cast<jboolean>(receiving),
            talkerStr.object());
    }
}

void ReflectorClient::updateServiceTransmitState(bool transmitting)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        serviceInstance.callMethod<void>("updateTransmitState", "(Z)V",
            static_cast<jboolean>(transmitting));
    }
}

void ReflectorClient::saveConnectionState()
{
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for saving reconnect profile";
        return;
    }
    QJniObject hostStr = QJniObject::fromString(m_host);
    QJniObject callsignStr = QJniObject::fromString(m_callsign);
    QJniObject authKeyStr = QJniObject::fromString(QString::fromUtf8(m_authKey));
    QJniObject monitoredTalkgroupsStr = QJniObject::fromString(m_monitoredTalkgroupsSpec);

    QJniObject::callStaticMethod<void>("yo6say/latry/ConnectionProfileStore",
        "saveConnectionState",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;ILjava/lang/String;Ljava/lang/String;I)V",
        context.object(),
        hostStr.object(),
        m_port,
        callsignStr.object(),
        m_talkgroup,
        authKeyStr.object(),
        monitoredTalkgroupsStr.object(),
        m_tgSelectTimeoutSeconds
    );
}

void ReflectorClient::clearConnectionState()
{
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "Failed to get Android context for clearing reconnect state";
        return;
    }

    QJniObject::callStaticMethod<void>("yo6say/latry/ConnectionProfileStore",
        "clearConnectionState",
        "(Landroid/content/Context;)V",
        context.object()
    );
}

#endif // Q_OS_ANDROID
