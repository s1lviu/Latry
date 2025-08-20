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

#include <QGuiApplication>
#include <QCoreApplication>
#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#endif
#if defined(Q_OS_IOS)
#  include "ios/IOSVoIPHandler.h"
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "ReflectorClient.h"
#include "BatteryOptimizationHandler.h"
#include <QtQuickControls2/QQuickStyle>
#include <QTimer>
#include <QQuickWindow>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
#if defined(Q_OS_ANDROID)
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
    
    // VoIP-specific Qt Android environment variables
    qputenv("QT_ANDROID_NO_EXIT_CALL", "1");  // Prevent crashes with background threads
    qputenv("QT_ANDROID_BACKGROUND_ACTIONS_QUEUE_SIZE", "50");  // Limit UI queue for VoIP
    
    // Service runs in same process as main application for JNI communication
#endif

#if defined(Q_OS_IOS)
    // iOS-specific VoIP environment configuration
    qputenv("QT_IOS_ALLOW_BACKGROUND_AUDIO", "1");  // Enable background audio processing
    qputenv("QT_IOS_DISABLE_APP_DELEGATE_SWIZZLING", "0");  // Allow Qt to handle app lifecycle
    
    // Configure for VoIP background mode
    qDebug() << "iOS VoIP app initialization";
#endif

    // Main application execution
    QGuiApplication app(argc, argv);
    
#if defined(Q_OS_ANDROID)
    // Quit when the last window closes (maps to Android back button close)
    // This preserves the "exit on back" behavior for Android without needing Qt.quit() inside onClosing
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QCoreApplication::quit);
#endif
    
#if defined(Q_OS_ANDROID)
    // Keep app running when minimized to maintain VoIP connection
    app.setQuitOnLastWindowClosed(false);
#endif

#if defined(Q_OS_IOS)
    // Keep iOS app running when minimized to maintain VoIP connection
    app.setQuitOnLastWindowClosed(false);
    
    // Initialize iOS VoIP handler early
    IOSVoIPHandler::instance();
    qDebug() << "iOS VoIP handler initialized";
#endif
    
    // Set application identifiers to fix Settings warnings
    app.setOrganizationName("YO6SAY");
    app.setOrganizationDomain("145500.xyz");
    app.setApplicationName("Latry");
    
#if defined(Q_OS_ANDROID)
    // Delay RECORD_AUDIO permission request until the user presses PTT.
    // Other permissions are declared in the manifest and do not need runtime confirmation.
#endif

    QQmlApplicationEngine engine;

    // Register our C++ client as a singleton accessible from QML
    // Use lambda to ensure singleton is created on the QML thread
    qmlRegisterSingletonType<ReflectorClient>("SvxlinkReflector.Client", 1, 0, "ReflectorClient", 
        [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            
            // Create singleton on this thread (QML thread)
            static ReflectorClient* client = nullptr;
            if (!client) {
                client = new ReflectorClient();
            }
            return client;
        });

    qmlRegisterSingletonType<BatteryOptimizationHandler>("SvxlinkReflector.Client", 1, 0, "BatteryOptimizationHandler",
        [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return new BatteryOptimizationHandler();
        });

    const QUrl url(u"qrc:/Main.qml"_s);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);

#if defined(Q_OS_ANDROID)
    // Fix Qt 6.9 Android applicationStateChanged crash - simplified safe approach
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     [](Qt::ApplicationState state) {
        qDebug() << "Application state changed to:" << state;
        
        // Simple approach: Let Qt handle graphics resources automatically
        // The original crash was due to manual resource management during state transitions
        // Qt 6.9 handles this internally, so we just log the state change
        
        if (state == Qt::ApplicationSuspended) {
            qDebug() << "App suspended - Qt will handle graphics resources automatically";
        } else if (state == Qt::ApplicationActive) {
            qDebug() << "App resumed - Qt will recreate resources as needed";
        }
    }, Qt::QueuedConnection);
#endif

#if defined(Q_OS_IOS)
    // iOS application state handling for VoIP background mode
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     [](Qt::ApplicationState state) {
        qDebug() << "iOS Application state changed to:" << state;
        
        IOSVoIPHandler* voipHandler = IOSVoIPHandler::instance();
        
        switch (state) {
            case Qt::ApplicationSuspended:
                qDebug() << "iOS App suspended - maintaining VoIP background task";
                if (voipHandler->isServiceRunning()) {
                    voipHandler->acquireBackgroundTask();
                }
                break;
                
            case Qt::ApplicationActive:
                qDebug() << "iOS App resumed - releasing background task and clearing audio buffers";
                voipHandler->releaseBackgroundTask();
                
                // Clear any stale audio when returning to foreground
                // This prevents delayed audio from playing when resuming the app
                QTimer::singleShot(100, []() {
                    // Find ReflectorClient instance and clear audio buffers
                    for (QObject* obj : QCoreApplication::instance()->children()) {
                        if (QString(obj->metaObject()->className()) == "ReflectorClient") {
                            QMetaObject::invokeMethod(obj, [obj]() {
                                QObject* audioEngine = obj->property("audioEngine").value<QObject*>();
                                if (audioEngine) {
                                    QMetaObject::invokeMethod(audioEngine, "flushAudioBuffers", Qt::QueuedConnection);
                                    qDebug() << "iOS: Cleared audio buffers on app resume";
                                }
                            }, Qt::QueuedConnection);
                            break;
                        }
                    }
                });
                break;
                
            case Qt::ApplicationInactive:
                qDebug() << "iOS App inactive";
                break;
                
            default:
                qDebug() << "iOS App state:" << state;
                break;
        }
    });
#endif

    return app.exec();
}
