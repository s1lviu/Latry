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

    // Main application execution
    QGuiApplication app(argc, argv);
    
#if defined(Q_OS_ANDROID)
    // Keep app running when minimized to maintain VoIP connection
    app.setQuitOnLastWindowClosed(false);
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
    // Fix Qt 6.9 Android applicationStateChanged crash
    // Qt QQuickWindow documentation: "releaseResources() attempts to release cached graphics resources"
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     [&engine](Qt::ApplicationState state) {
        qDebug() << "Application state changed to:" << state;
        
        if (state == Qt::ApplicationSuspended) {
            qDebug() << "App suspended - releasing graphics resources to prevent Qt OpenGL crash";
            // Get all QQuickWindows and release their graphics resources
            for (QWindow *window : QGuiApplication::allWindows()) {
                if (QQuickWindow *quickWindow = qobject_cast<QQuickWindow*>(window)) {
                    quickWindow->releaseResources();
                }
            }
            // Process any pending events before suspend
            QGuiApplication::processEvents();
            
        } else if (state == Qt::ApplicationActive) {
            qDebug() << "App resumed - graphics resources will be recreated automatically";
            // Qt automatically recreates resources when needed
        }
    });
#endif

    return app.exec();
}
