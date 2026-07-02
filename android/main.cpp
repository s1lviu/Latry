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

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QStringList>
#include "ReflectorClient.h"
#include "AppLaunchMode.h"
#include "BatteryOptimizationHandler.h"
#include <QtQuickControls2/QQuickStyle>

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

    QStringList launchArguments;
    launchArguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        launchArguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const AppLaunchMode launchMode = detectAppLaunchMode(launchArguments);

    const auto runApp = [launchMode](QCoreApplication &app) -> int {
        qInfo() << "Latry launch mode:"
                << (launchMode == AppLaunchMode::AndroidService ? "android-service" : "ui");

        app.setOrganizationName("YO6SAY");
        app.setOrganizationDomain("145500.xyz");
        app.setApplicationName("Latry");

        // Force singleton creation on the Qt main thread before any JNI callback can touch it.
        ReflectorClient *reflectorClient = ReflectorClient::instance();
        if (!reflectorClient) {
            qCritical() << "Failed to create ReflectorClient singleton";
            return -1;
        }

        if (launchMode == AppLaunchMode::AndroidService) {
            qInfo() << "Android service launch detected - using headless Qt core mode";
            return app.exec();
        }

        auto &guiApp = static_cast<QGuiApplication &>(app);

#if defined(Q_OS_ANDROID)
        // Keep the UI process alive when minimized to maintain the foreground service connection.
        guiApp.setQuitOnLastWindowClosed(false);
#endif

        QQmlApplicationEngine engine;

        // Register the already-created singleton instance so QML uses the same object as JNI.
        qmlRegisterSingletonInstance<ReflectorClient>(
            "SvxlinkReflector.Client", 1, 0, "ReflectorClient", reflectorClient);

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

        return guiApp.exec();
    };

    if (launchMode == AppLaunchMode::AndroidService) {
        QCoreApplication app(argc, argv);
        return runApp(app);
    }

    QGuiApplication app(argc, argv);
    return runApp(app);
}
