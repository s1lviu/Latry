#include <QtTest>

#include "AndroidAudioRouteInterop.h"

class AndroidAudioRouteInteropTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizeRouteId_defaultsUnknownRoutesToSpeaker();
    void parseRoutesJson_normalizesDeduplicatesAndPreservesSpeakerFallback();
    void parseRoutesJson_ignoresInvalidPayloads();
};

void AndroidAudioRouteInteropTest::normalizeRouteId_defaultsUnknownRoutesToSpeaker()
{
    QCOMPARE(AndroidAudioRouteInterop::normalizeRouteId(QString()), QStringLiteral("speaker"));
    QCOMPARE(AndroidAudioRouteInterop::normalizeRouteId(QStringLiteral(" bluetooth ")), QStringLiteral("bluetooth"));
    QCOMPARE(AndroidAudioRouteInterop::normalizeRouteId(QStringLiteral("WIRED_HEADSET")), QStringLiteral("wired_headset"));
    QCOMPARE(AndroidAudioRouteInterop::normalizeRouteId(QStringLiteral("car")), QStringLiteral("speaker"));
}

void AndroidAudioRouteInteropTest::parseRoutesJson_normalizesDeduplicatesAndPreservesSpeakerFallback()
{
    const QStringList routes = AndroidAudioRouteInterop::parseRoutesJson(
        QStringLiteral(R"(["bluetooth"," wired_headset ","bluetooth","speaker","car"])"));

    QCOMPARE(routes, QStringList({
        QStringLiteral("bluetooth"),
        QStringLiteral("wired_headset"),
        QStringLiteral("speaker")
    }));
}

void AndroidAudioRouteInteropTest::parseRoutesJson_ignoresInvalidPayloads()
{
    QCOMPARE(AndroidAudioRouteInterop::parseRoutesJson(QStringLiteral(R"({"routes":["bluetooth"]})")),
             QStringList({QStringLiteral("speaker")}));
    QCOMPARE(AndroidAudioRouteInterop::parseRoutesJson(QStringLiteral(R"([1,true,null])")),
             QStringList({QStringLiteral("speaker")}));
    QCOMPARE(AndroidAudioRouteInterop::parseRoutesJson(QStringLiteral("not-json")),
             QStringList({QStringLiteral("speaker")}));
}

QTEST_GUILESS_MAIN(AndroidAudioRouteInteropTest)

#include "tst_android_audio_route_interop.moc"
