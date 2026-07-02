#include <QDebug>

#if defined(Q_OS_ANDROID)
#include <QtCore/private/qandroidextras_p.h>
#else
#include <QCoreApplication>
#endif

#include "ReflectorClient.h"

int main(int argc, char *argv[])
{
#if defined(Q_OS_ANDROID)
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
    qputenv("QT_ANDROID_NO_EXIT_CALL", "1");
    qputenv("QT_ANDROID_BACKGROUND_ACTIONS_QUEUE_SIZE", "50");
#endif

#if defined(Q_OS_ANDROID)
    // QAndroidService releases Qt's internal service startup barrier so
    // QtServiceBase.onCreate() can return before Android's ANR timeout.
    QAndroidService app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif
    app.setOrganizationName("YO6SAY");
    app.setOrganizationDomain("145500.xyz");
    app.setApplicationName("Latry");

    qInfo() << "Latry dedicated Android service library starting";

    ReflectorClient *reflectorClient = ReflectorClient::instance();
    if (!reflectorClient) {
        qCritical() << "Failed to create ReflectorClient singleton in service mode";
        return -1;
    }

    return app.exec();
}
