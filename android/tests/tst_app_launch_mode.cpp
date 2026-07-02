#include <QtTest>

#include "AppLaunchMode.h"

class AppLaunchModeTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsToUserInterface();
    void detectsAndroidServiceArgument();
    void ignoresUnrelatedArguments();
};

void AppLaunchModeTest::defaultsToUserInterface()
{
    QVERIFY(detectAppLaunchMode({QStringLiteral("latry")}) == AppLaunchMode::UserInterface);
    QVERIFY(!isAndroidServiceLaunchMode({QStringLiteral("latry")}));
}

void AppLaunchModeTest::detectsAndroidServiceArgument()
{
    const QStringList arguments{
        QStringLiteral("latry"),
        QStringLiteral("--verbose"),
        QString::fromLatin1(kAndroidServiceLaunchArgument)
    };

    QVERIFY(detectAppLaunchMode(arguments) == AppLaunchMode::AndroidService);
    QVERIFY(isAndroidServiceLaunchMode(arguments));
}

void AppLaunchModeTest::ignoresUnrelatedArguments()
{
    const QStringList arguments{
        QStringLiteral("latry"),
        QStringLiteral("--service"),
        QStringLiteral("--android")
    };

    QVERIFY(detectAppLaunchMode(arguments) == AppLaunchMode::UserInterface);
    QVERIFY(!isAndroidServiceLaunchMode(arguments));
}

QTEST_APPLESS_MAIN(AppLaunchModeTest)

#include "tst_app_launch_mode.moc"
