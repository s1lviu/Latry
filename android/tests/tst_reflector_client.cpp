#include <QtTest>

#include "ReflectorClient.h"
#include "ReflectorProtocol.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QMessageAuthenticationCode>
#include <QSignalSpy>
#include <QtEndian>

#include <QElapsedTimer>

#include <cstring>

class FakeTcpSocket : public QTcpSocket
{
public:
    explicit FakeTcpSocket(QObject *parent = nullptr)
        : QTcpSocket(parent)
    {
        open(QIODevice::ReadWrite);
        setSocketState(QAbstractSocket::ConnectedState);
        setLocalAddress(QHostAddress(QHostAddress::LocalHost));
        setLocalPort(42000);
        setPeerAddress(QHostAddress(QHostAddress::LocalHost));
        setPeerPort(43000);
    }

    void queueIncoming(const QByteArray &data)
    {
        m_incoming.append(data);
    }

    QByteArray takeOutgoing()
    {
        QByteArray data = m_outgoing;
        m_outgoing.clear();
        return data;
    }

    qint64 bytesAvailable() const override
    {
        return QTcpSocket::bytesAvailable() + m_incoming.size();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 bytesToRead = qMin(maxlen, qint64(m_incoming.size()));
        if (bytesToRead <= 0)
            return -1;
        memcpy(data, m_incoming.constData(), size_t(bytesToRead));
        m_incoming.remove(0, int(bytesToRead));
        return bytesToRead;
    }

    qint64 writeData(const char *data, qint64 len) override
    {
        m_outgoing.append(data, int(len));
        return len;
    }

private:
    QByteArray m_incoming;
    QByteArray m_outgoing;
};

class ReflectorClientTest : public QObject
{
    Q_OBJECT

private slots:
    void parseMonitoredTalkgroupsSpecNormalizesEntries();
    void updateProfileConfigurationNormalizesTalkgroupTimeout();
    void txTimeoutDefaultsToEnabled175Seconds();
    void txTimeoutRejectsNonPositiveValues();
    void txTimeoutAcceptsArbitraryPositiveValue();
    void txTimeoutReleasesPttAfterLimit();
    void txTimeoutWarningActivatesInLastTenSeconds();
    void txTimeoutWarningClearsWhenPttStops();
    void txTimeoutWarningFeedbackWaitsForTimerTick();
    void txTimeoutWarningFeedbackResetsWhenTransmissionStops();
    void pttHangTimeDefaultsTo100Ms();
    void pttHangTimeNormalizesConfiguredValues();
#if defined(Q_OS_ANDROID)
    void androidHardwarePttReleaseCancelsPendingPermissionRetry();
    void androidExternalPttPressBeforeAudioReadyQueuesResume();
    void androidNetworkHandoverWhilePttActiveQueuesResume();
#endif
    void pttReleaseHonorsConfiguredHangTime();
    void pttPressedCancelsPendingHangtimeRelease();
    void forcePttReleaseBypassesHangtime();
    void togglePttForcesImmediateStopDuringPendingRelease();
    void pttReleasedCancelsPendingPermissionRetry();
    void liveTranscriptionDefaultsToDisabledAndUnavailable();
    void disablingLiveTranscriptionClearsPendingPermissionRetry();
    void liveTranscriptionComposesPartialAndFinalText();
    void setReceivingAudioStateFalseClearsTranscriptionText();
    void transcriptionLanguageModelsSeparateInstalledAndDownloadableEntries();
    void transcriptionModelDownloadStateTracksProgress();
    void languageUnavailableErrorDisablesLiveTranscription();
    void sanitizeCustomNodeInfoEntriesDropsReservedAndEmptyValues();
    void setAudioRouteStateNormalizesAndOrdersRoutes();
    void shouldHandleTalkerStartUsesPriorityRules();
    void talkgroupSelectionTimeoutRevertsToMonitorMode();
    void sendProtoVerWritesExpectedFrame();
    void authChallengeFrameProducesAuthResponse();
    void errorFrameClearsCachedAuthKey();
    void requestQsyFrameUpdatesTalkgroupAndResponds();
    void nodeListFrameDecodesLengthPrefixedEntries();
    void nodeJoinLeaveFramesDecodeLengthPrefixedCallsigns();
    void malformedNodeFramesAreIgnoredWithoutDisconnect();
    void validatedNetworkLossMovesClientToWaitingState();
    void validatedNetworkRestorationSchedulesImmediateReconnect();
    void validatedRouteChangeForcesReconnect();
    void inboundHeartbeatsArmProtocolLivenessWatchdog();
    void protocolLivenessTimeoutSchedulesReconnect();

    // Shutdown / ANR-fix tests
    void prepareForShutdownSetsShutdownFlag();
    void prepareForShutdownIsIdempotent();
    void prepareForShutdownStopsAllTimers();
    void prepareForShutdownStopsAudioThread();
    void prepareForShutdownAbortsNetworkReply();
    void destructorIsNoOpAfterPrepareForShutdown();
    void destructorFallbackStopsAudioThread();
    void aboutToQuitTriggersPrepareForShutdown();
    void audioThreadWaitHasBoundedTimeout();

private:
    FakeTcpSocket *installFakeTcpSocket(ReflectorClient &client);
    QByteArray framedPayload(const QByteArray &payload) const;
    QByteArray decodeSingleOutgoingPayload(FakeTcpSocket *socket) const;
    void feedIncomingPayload(ReflectorClient &client, FakeTcpSocket *socket, const QByteArray &payload);
};

FakeTcpSocket *ReflectorClientTest::installFakeTcpSocket(ReflectorClient &client)
{
    QTcpSocket *originalSocket = client.m_tcpSocket;
    originalSocket->disconnect(&client);
    originalSocket->deleteLater();

    auto *socket = new FakeTcpSocket(&client);
    client.m_tcpSocket = socket;
    QObject::connect(socket, &QTcpSocket::connected, &client, &ReflectorClient::onTcpConnected);
    QObject::connect(socket, &QTcpSocket::disconnected, &client, &ReflectorClient::onTcpDisconnected);
    QObject::connect(socket, &QTcpSocket::readyRead, &client, &ReflectorClient::onTcpReadyRead);
    QObject::connect(socket, &QTcpSocket::errorOccurred, &client, &ReflectorClient::onTcpError);
    return socket;
}

QByteArray ReflectorClientTest::framedPayload(const QByteArray &payload) const
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(payload.size());
    frame.append(payload);
    return frame;
}

QByteArray ReflectorClientTest::decodeSingleOutgoingPayload(FakeTcpSocket *socket) const
{
    const QByteArray frame = socket->takeOutgoing();
    if (frame.size() < int(sizeof(quint32)))
        return {};

    const quint32 payloadSize = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(frame.constData()));
    if (frame.size() != int(sizeof(quint32) + payloadSize))
        return {};

    return frame.mid(sizeof(quint32), payloadSize);
}

void ReflectorClientTest::feedIncomingPayload(ReflectorClient &client, FakeTcpSocket *socket,
                                              const QByteArray &payload)
{
    socket->queueIncoming(framedPayload(payload));
    client.onTcpReadyRead();
}

void ReflectorClientTest::parseMonitoredTalkgroupsSpecNormalizesEntries()
{
    const auto parsed = ReflectorClient::parseMonitoredTalkgroupsSpec(
        QStringLiteral("  3100+, abc, 0, 3200++, 3100, 91+++ "),
        9);

    QCOMPARE(parsed.normalizedSpec, QStringLiteral("3100+,3200++,91+++"));
    QCOMPARE(parsed.configured.size(), 3);
    QCOMPARE(parsed.configured.at(0).talkgroup, 3100u);
    QCOMPARE(parsed.configured.at(0).priority, quint8(1));
    QCOMPARE(parsed.configured.at(1).talkgroup, 3200u);
    QCOMPARE(parsed.configured.at(1).priority, quint8(2));
    QCOMPARE(parsed.configured.at(2).talkgroup, 91u);
    QCOMPARE(parsed.configured.at(2).priority, quint8(3));
    QCOMPARE(parsed.activeTalkgroups, QList<quint32>({9u, 3100u, 3200u, 91u}));
}

void ReflectorClientTest::updateProfileConfigurationNormalizesTalkgroupTimeout()
{
    ReflectorClient client;

    client.updateProfileConfiguration(91, QStringLiteral("3100+"), 45);
    QCOMPARE(client.m_defaultTalkgroup, 91u);
    QCOMPARE(client.m_monitoredTalkgroups, QList<quint32>({91u, 3100u}));
    QCOMPARE(client.m_tgSelectTimeoutSeconds, 45);

    client.updateProfileConfiguration(91, QStringLiteral("3100+"), 0);
    QCOMPARE(client.m_tgSelectTimeoutSeconds, 30);
}

void ReflectorClientTest::txTimeoutDefaultsToEnabled175Seconds()
{
    ReflectorClient client;

    QVERIFY(client.m_txTimeoutEnabled);
    QCOMPARE(client.m_txTimeoutSeconds, 175);
}

void ReflectorClientTest::txTimeoutRejectsNonPositiveValues()
{
    ReflectorClient client;

    client.setTxTimeoutSeconds(25);
    QCOMPARE(client.m_txTimeoutSeconds, 25);

    client.setTxTimeoutSeconds(0);
    QCOMPARE(client.m_txTimeoutSeconds, 175);

    client.setTxTimeoutSeconds(-9);
    QCOMPARE(client.m_txTimeoutSeconds, 175);
}

void ReflectorClientTest::txTimeoutAcceptsArbitraryPositiveValue()
{
    ReflectorClient client;
    QSignalSpy pttSpy(&client, &ReflectorClient::pttActiveChanged);

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.m_txTimeoutSeconds = 1;
    client.m_txSeconds = 0;

    client.onTxTimerTimeout();

    QTRY_VERIFY(!client.m_pttActive);
    QCOMPARE(client.m_txSeconds, 0);
    QVERIFY(pttSpy.count() >= 1);
}

void ReflectorClientTest::txTimeoutReleasesPttAfterLimit()
{
    ReflectorClient client;
    QSignalSpy pttSpy(&client, &ReflectorClient::pttActiveChanged);
    QSignalSpy txTimeSpy(&client, &ReflectorClient::txTimeStringChanged);

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.m_txSeconds = client.m_txTimeoutSeconds - 1;

    client.onTxTimerTimeout();

    QTRY_VERIFY(!client.m_pttActive);
    QCOMPARE(client.m_txSeconds, 0);
    QCOMPARE(client.txTimeString(), QStringLiteral("00:00"));
    QCOMPARE(client.m_txStopPending, false);
    QVERIFY(pttSpy.count() >= 1);
    QVERIFY(txTimeSpy.count() >= 2);
}

void ReflectorClientTest::txTimeoutWarningActivatesInLastTenSeconds()
{
    ReflectorClient client;
    QSignalSpy warningSpy(&client, &ReflectorClient::txTimeoutWarningChanged);

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.m_txSeconds = client.m_txTimeoutSeconds - 11;

    client.onTxTimerTimeout();

    QVERIFY(client.txTimeoutWarning());
    QCOMPARE(warningSpy.count(), 1);
}

void ReflectorClientTest::txTimeoutWarningClearsWhenPttStops()
{
    ReflectorClient client;
    QSignalSpy warningSpy(&client, &ReflectorClient::txTimeoutWarningChanged);

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.m_txSeconds = client.m_txTimeoutSeconds - 10;
    client.updateTxTimeoutWarningState();

    QVERIFY(client.txTimeoutWarning());

    client.pttReleased();

    QVERIFY(!client.txTimeoutWarning());
    QTRY_VERIFY(!client.m_pttActive);
    QVERIFY(warningSpy.count() >= 2);
}

void ReflectorClientTest::txTimeoutWarningFeedbackWaitsForTimerTick()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.m_txTimeoutSeconds = 10;
    client.m_txSeconds = 0;

    client.updateTxTimeoutWarningState();

    QVERIFY(client.txTimeoutWarning());
    QVERIFY(!client.m_txTimeoutWarningFeedbackSent);

    client.onTxTimerTimeout();

    QVERIFY(client.m_txTimeoutWarningFeedbackSent);
}

void ReflectorClientTest::txTimeoutWarningFeedbackResetsWhenTransmissionStops()
{
    ReflectorClient client;
    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = true;
    client.m_txSeconds = client.m_txTimeoutSeconds - 5;
    client.m_txTimeoutWarning = true;
    client.m_txTimeoutWarningFeedbackSent = true;

    client.onTxDrainComplete();

    QVERIFY(!client.m_pttActive);
    QVERIFY(!client.m_txTimeoutWarning);
    QVERIFY(!client.m_txTimeoutWarningFeedbackSent);
    QCOMPARE(client.m_txSeconds, 0);
}

void ReflectorClientTest::pttHangTimeDefaultsTo100Ms()
{
    ReflectorClient client;

    QCOMPARE(client.m_pttHangTimeMs, 100);
}

void ReflectorClientTest::pttHangTimeNormalizesConfiguredValues()
{
    ReflectorClient client;

    client.setPttHangTimeMs(250);
    QCOMPARE(client.m_pttHangTimeMs, 250);

    client.setPttHangTimeMs(0);
    QCOMPARE(client.m_pttHangTimeMs, 0);

    client.setPttHangTimeMs(-1);
    QCOMPARE(client.m_pttHangTimeMs, 100);

    client.setPttHangTimeMs(2500);
    QCOMPARE(client.m_pttHangTimeMs, 1000);
}

#if defined(Q_OS_ANDROID)
void ReflectorClientTest::androidHardwarePttReleaseCancelsPendingPermissionRetry()
{
    ReflectorClient client;

    client.m_pttPermissionRestartPending = true;

    client.handleAndroidControlEvent(2); // AndroidControlPttRelease

    QVERIFY(!client.m_pttPermissionRestartPending);
}

void ReflectorClientTest::androidExternalPttPressBeforeAudioReadyQueuesResume()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;
    client.m_audioReady = false;

    client.handleAndroidControlEvent(1); // AndroidControlPttPress

    QVERIFY(client.m_resumeAndroidPttAfterReconnect);
    QVERIFY(!client.m_pttActive);

    client.handleAndroidControlEvent(2); // AndroidControlPttRelease

    QVERIFY(!client.m_resumeAndroidPttAfterReconnect);
}

void ReflectorClientTest::androidNetworkHandoverWhilePttActiveQueuesResume()
{
    ReflectorClient client;

    client.m_host = QStringLiteral("reflector.example");
    client.m_port = 5337;
    client.m_authKey = QByteArrayLiteral("secret");
    client.m_callsign = QStringLiteral("YO6SAY");
    client.m_talkgroup = 9;
    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;

    client.handleAndroidNetworkStateChanged(1, 1, true, true, 1, false, false, false);
    client.handleAndroidNetworkStateChanged(2, 4, true, true, 2, false, false, true);

    QCOMPARE(client.m_state, ReflectorClient::Disconnected);
    QVERIFY(client.m_resumeAndroidPttAfterReconnect);

    client.handleAndroidControlEvent(2); // AndroidControlPttRelease

    QVERIFY(!client.m_resumeAndroidPttAfterReconnect);

    client.m_resumeAndroidPttAfterReconnect = true;
    client.pttReleased();
    QVERIFY(!client.m_resumeAndroidPttAfterReconnect);
}
#endif

void ReflectorClientTest::pttReleaseHonorsConfiguredHangTime()
{
    ReflectorClient client;
    QSignalSpy pttSpy(&client, &ReflectorClient::pttActiveChanged);

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.setPttHangTimeMs(60);

    client.pttReleased();

    QVERIFY(client.m_pttReleasePending);
    QVERIFY(client.m_pttActive);
    QVERIFY(!client.m_txStopPending);

    QTest::qWait(20);
    QVERIFY(client.m_pttActive);
    QVERIFY(client.m_pttReleasePending);

    QTRY_VERIFY_WITH_TIMEOUT(!client.m_pttActive, 500);
    QVERIFY(!client.m_pttReleasePending);
    QVERIFY(pttSpy.count() >= 1);
}

void ReflectorClientTest::pttPressedCancelsPendingHangtimeRelease()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.setPttHangTimeMs(80);

    client.pttReleased();
    QVERIFY(client.m_pttReleasePending);

    client.pttPressed();

    QVERIFY(!client.m_pttReleasePending);
    QTest::qWait(120);
    QVERIFY(client.m_pttActive);
    QVERIFY(!client.m_txStopPending);
}

void ReflectorClientTest::forcePttReleaseBypassesHangtime()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.setPttHangTimeMs(250);

    client.pttReleased();
    QVERIFY(client.m_pttReleasePending);

    client.forcePttRelease();

    QVERIFY(!client.m_pttReleasePending);
    QTRY_VERIFY_WITH_TIMEOUT(!client.m_pttActive, 250);
}

void ReflectorClientTest::togglePttForcesImmediateStopDuringPendingRelease()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;
    client.m_pttActive = true;
    client.m_txStopPending = false;
    client.setPttHangTimeMs(250);

    client.pttReleased();
    QVERIFY(client.m_pttReleasePending);

    client.togglePtt();

    QVERIFY(!client.m_pttReleasePending);
    QTRY_VERIFY_WITH_TIMEOUT(!client.m_pttActive, 250);
}

void ReflectorClientTest::pttReleasedCancelsPendingPermissionRetry()
{
    ReflectorClient client;

    client.m_pttPermissionRestartPending = true;

    client.pttReleased();

    QVERIFY(!client.m_pttPermissionRestartPending);
    QVERIFY(!client.m_pttActive);
}

void ReflectorClientTest::liveTranscriptionDefaultsToDisabledAndUnavailable()
{
    ReflectorClient client;

    QVERIFY(!client.liveTranscriptionEnabled());
    QVERIFY(client.transcriptionText().isEmpty());
    QVERIFY(!client.transcriptionAvailable());
}

void ReflectorClientTest::disablingLiveTranscriptionClearsPendingPermissionRetry()
{
    ReflectorClient client;

    client.m_transcriptionPermissionEnablePending = true;

    client.setLiveTranscriptionEnabled(false);

    QVERIFY(!client.m_transcriptionPermissionEnablePending);
}

void ReflectorClientTest::liveTranscriptionComposesPartialAndFinalText()
{
    ReflectorClient client;
    QSignalSpy textSpy(&client, &ReflectorClient::transcriptionTextChanged);

    client.m_liveTranscriptionEnabled = true;
    client.m_transcriptionSessionActive = true;

    client.handlePartialTranscription(QStringLiteral("  cq   latry "));
    QCOMPARE(client.transcriptionText(), QStringLiteral("cq latry"));

    client.handleFinalTranscription(QStringLiteral("CQ Latry"));
    QCOMPARE(client.transcriptionText(), QStringLiteral("CQ Latry"));

    client.handlePartialTranscription(QStringLiteral("testing audio"));
    QCOMPARE(client.transcriptionText(), QStringLiteral("CQ Latry\ntesting audio"));

    client.handleFinalTranscription(QStringLiteral("testing audio"));
    QCOMPARE(client.transcriptionText(), QStringLiteral("CQ Latry\ntesting audio"));

    client.handleFinalTranscription(QStringLiteral("testing audio"));
    QCOMPARE(client.transcriptionText(), QStringLiteral("CQ Latry\ntesting audio"));
    QVERIFY(textSpy.count() >= 3);
}

void ReflectorClientTest::setReceivingAudioStateFalseClearsTranscriptionText()
{
    ReflectorClient client;

    client.m_liveTranscriptionEnabled = true;
    client.m_transcriptionCommittedText = QStringLiteral("CQ test");
    client.m_transcriptionPendingText = QStringLiteral("partial");
    client.updateTranscriptionDisplay();

    QVERIFY(!client.transcriptionText().isEmpty());

    client.setReceivingAudioState(false);

    QVERIFY(client.transcriptionText().isEmpty());
}

void ReflectorClientTest::transcriptionLanguageModelsSeparateInstalledAndDownloadableEntries()
{
    ReflectorClient client;
    QSignalSpy modelsSpy(&client, &ReflectorClient::transcriptionLanguageModelsChanged);

    client.setTranscriptionLanguageModels(QStringList{QStringLiteral("en-US")},
                                          QStringList{QStringLiteral("de-DE")},
                                          QStringList{QStringLiteral("de-DE"),
                                                      QStringLiteral("pl-PL")},
                                          QStringLiteral("de-DE"));

    QCOMPARE(modelsSpy.count(), 1);
    QCOMPARE(client.transcriptionInstalledLanguages().size(), 1);
    QCOMPARE(client.transcriptionDownloadableLanguages().size(), 2);

    const QVariantMap installed = client.transcriptionInstalledLanguages().constFirst().toMap();
    QCOMPARE(installed.value(QStringLiteral("tag")).toString(), QStringLiteral("en-US"));
    QCOMPARE(installed.value(QStringLiteral("status")).toString(), QStringLiteral("Installed"));

    const QVariantMap pending = client.transcriptionDownloadableLanguages().constFirst().toMap();
    QCOMPARE(pending.value(QStringLiteral("tag")).toString(), QStringLiteral("de-DE"));
    QVERIFY(pending.value(QStringLiteral("pending")).toBool());
    QVERIFY(pending.value(QStringLiteral("activeDownload")).toBool());
    QCOMPARE(pending.value(QStringLiteral("status")).toString(),
             QStringLiteral("Download in progress"));

    const QVariantMap downloadable = client.transcriptionDownloadableLanguages().constLast().toMap();
    QCOMPARE(downloadable.value(QStringLiteral("tag")).toString(), QStringLiteral("pl-PL"));
    QVERIFY(!downloadable.value(QStringLiteral("pending")).toBool());
    QVERIFY(!downloadable.value(QStringLiteral("activeDownload")).toBool());
    QCOMPARE(downloadable.value(QStringLiteral("status")).toString(),
             QStringLiteral("Available to download"));
}

void ReflectorClientTest::transcriptionModelDownloadStateTracksProgress()
{
    ReflectorClient client;
    QSignalSpy stateSpy(&client, &ReflectorClient::transcriptionModelDownloadStateChanged);

    client.setTranscriptionModelDownloadState(true,
                                              true,
                                              42,
                                              QStringLiteral("Downloading on-device speech model (42%)"));

    QVERIFY(client.transcriptionModelDownloadAvailable());
    QVERIFY(client.transcriptionModelDownloadInProgress());
    QCOMPARE(client.transcriptionModelDownloadProgress(), 42);
    QCOMPARE(client.transcriptionModelDownloadStatus(),
             QStringLiteral("Downloading on-device speech model (42%)"));
    QCOMPARE(stateSpy.count(), 1);
}

void ReflectorClientTest::languageUnavailableErrorDisablesLiveTranscription()
{
    ReflectorClient client;
    QSignalSpy enabledSpy(&client, &ReflectorClient::liveTranscriptionEnabledChanged);

    client.m_liveTranscriptionEnabled = true;
    client.m_transcriptionAvailable = true;
    client.m_transcriptionSessionActive = true;

    client.handleTranscriptionError(13, QStringLiteral("Language model unavailable on device"));

    QVERIFY(!client.liveTranscriptionEnabled());
    QVERIFY(!client.m_transcriptionSessionActive);
    QCOMPARE(enabledSpy.count(), 1);
}

void ReflectorClientTest::sanitizeCustomNodeInfoEntriesDropsReservedAndEmptyValues()
{
    const QVariantList entries{
        QVariantMap{{QStringLiteral("key"), QStringLiteral("sw")},
                    {QStringLiteral("value"), QStringLiteral("blocked")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral(" Callsign ")},
                    {QStringLiteral("value"), QStringLiteral("blocked-too")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("city")},
                    {QStringLiteral("value"), QStringLiteral("  Cluj-Napoca  ")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("note")},
                    {QStringLiteral("value"), QStringLiteral("")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("rig")},
                    {QStringLiteral("value"), QStringLiteral("OpenRTX")}}
    };

    const QJsonObject sanitized = ReflectorClient::sanitizeCustomNodeInfoEntries(entries);

    QCOMPARE(sanitized.size(), 2);
    QCOMPARE(sanitized.value(QStringLiteral("city")).toString(), QStringLiteral("Cluj-Napoca"));
    QCOMPARE(sanitized.value(QStringLiteral("rig")).toString(), QStringLiteral("OpenRTX"));
    QVERIFY(!sanitized.contains(QStringLiteral("sw")));
    QVERIFY(!sanitized.contains(QStringLiteral(" Callsign ")));
}

void ReflectorClientTest::setAudioRouteStateNormalizesAndOrdersRoutes()
{
    ReflectorClient client;

    client.setAudioRouteState(QStringLiteral(" bluetooth "),
                              QStringList{QStringLiteral("wired_headset"),
                                          QStringLiteral("speaker"),
                                          QStringLiteral("BLUETOOTH"),
                                          QStringLiteral("speaker")});

    QCOMPARE(client.m_currentAudioRoute, QStringLiteral("bluetooth"));
    QCOMPARE(client.m_availableAudioRoutes.size(), 3);
    QCOMPARE(client.m_availableAudioRoutes.at(0).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("speaker"));
    QCOMPARE(client.m_availableAudioRoutes.at(1).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("wired_headset"));
    QCOMPARE(client.m_availableAudioRoutes.at(2).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("bluetooth"));

    client.setPreferredAudioRoute(QStringLiteral("unknown-route"));
    QCOMPARE(client.m_preferredAudioRoute, QStringLiteral("speaker"));
    QCOMPARE(client.m_currentAudioRoute, QStringLiteral("speaker"));
}

void ReflectorClientTest::shouldHandleTalkerStartUsesPriorityRules()
{
    ReflectorClient client;
    client.m_state = ReflectorClient::Connected;
    client.m_defaultTalkgroup = 91;
    client.applyMonitoredTalkgroups(QStringLiteral("3100+,3200++"));

    client.m_talkgroup = 0;
    client.m_usePriorityMode = true;
    QVERIFY(client.shouldHandleTalkerStart(3100));
    QCOMPARE(client.m_talkgroup, 3100u);

    client.m_talkgroup = 3100;
    client.m_usePriorityMode = true;
    QVERIFY(client.shouldHandleTalkerStart(3200));
    QCOMPARE(client.m_talkgroup, 3200u);

    client.m_talkgroup = 3100;
    client.m_usePriorityMode = false;
    QVERIFY(!client.shouldHandleTalkerStart(3200));
    QCOMPARE(client.m_talkgroup, 3100u);
}

void ReflectorClientTest::talkgroupSelectionTimeoutRevertsToMonitorMode()
{
    ReflectorClient client;
    installFakeTcpSocket(client);

    client.m_state = ReflectorClient::Connected;
    client.m_talkgroup = 91;
    client.updateProfileConfiguration(91, QStringLiteral("3100+"), 2);

    QVERIFY(client.m_talkgroupSelectionTimer->isActive());
    QCOMPARE(client.m_tgSelectTimeoutCounter, 2);
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Connected to TG 91"));

    client.onTalkgroupSelectionTimer();
    QCOMPARE(client.m_talkgroup, 91u);
    QCOMPARE(client.m_tgSelectTimeoutCounter, 1);

    client.onTalkgroupSelectionTimer();
    QCOMPARE(client.m_talkgroup, 0u);
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Connected in monitor mode"));
    QCOMPARE(client.m_tgSelectTimeoutCounter, 0);
    QVERIFY(!client.m_talkgroupSelectionTimer->isActive());
}

void ReflectorClientTest::sendProtoVerWritesExpectedFrame()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);

    client.sendProtoVer();

    const QByteArray payload = decodeSingleOutgoingPayload(socket);
    QVERIFY(!payload.isEmpty());

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);
    quint16 type = 0;
    quint16 major = 0;
    quint16 minor = 0;
    stream >> type >> major >> minor;

    QCOMPARE(type, quint16(Svxlink::MsgType::PROTO_VER));
    QCOMPARE(major, quint16(Svxlink::Protocol::MAJOR_VER));
    QCOMPARE(minor, quint16(Svxlink::Protocol::MINOR_VER));
}

void ReflectorClientTest::authChallengeFrameProducesAuthResponse()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);

    client.m_authKey = QByteArray("secret");
    client.m_callsign = QStringLiteral("YO6SAY");

    QByteArray challenge(Svxlink::Protocol::CHALLENGE_LEN, '\0');
    for (int i = 0; i < challenge.size(); ++i)
        challenge[i] = char(i + 1);

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::AUTH_CHALLENGE);
    stream << quint16(challenge.size());
    stream.writeRawData(challenge.constData(), challenge.size());

    feedIncomingPayload(client, socket, payload);

    const QByteArray responsePayload = decodeSingleOutgoingPayload(socket);
    QVERIFY(!responsePayload.isEmpty());

    QDataStream responseStream(responsePayload);
    responseStream.setByteOrder(QDataStream::BigEndian);
    quint16 type = 0;
    quint16 callsignLen = 0;
    quint16 digestLen = 0;
    responseStream >> type >> callsignLen;
    QByteArray callsignData(callsignLen, '\0');
    responseStream.readRawData(callsignData.data(), callsignData.size());
    responseStream >> digestLen;
    QByteArray digest(digestLen, '\0');
    responseStream.readRawData(digest.data(), digest.size());

    QCOMPARE(type, quint16(Svxlink::MsgType::AUTH_RESPONSE));
    QCOMPARE(QString::fromUtf8(callsignData), QStringLiteral("YO6SAY"));
    QCOMPARE(digestLen, quint16(Svxlink::Protocol::DIGEST_LEN));

    const QByteArray expectedDigest = QMessageAuthenticationCode::hash(
        challenge, QByteArray("secret"), QCryptographicHash::Sha1);
    QCOMPARE(digest, expectedDigest);
}

void ReflectorClientTest::errorFrameClearsCachedAuthKey()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);

    client.m_state = ReflectorClient::Authenticating;
    client.m_authKey = QByteArray("secret");

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    const QByteArray message("Access denied");
    stream << quint16(Svxlink::MsgType::ERROR);
    stream << quint16(message.size());
    stream.writeRawData(message.constData(), message.size());

    feedIncomingPayload(client, socket, payload);

    QCOMPARE(client.m_state, ReflectorClient::Disconnected);
    QCOMPARE(client.m_authKey, QByteArray());
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Server error: Access denied"));
}

void ReflectorClientTest::requestQsyFrameUpdatesTalkgroupAndResponds()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);
    QSignalSpy qsySpy(&client, &ReflectorClient::qsyRequested);

    client.m_state = ReflectorClient::Connected;
    client.m_talkgroup = 91;
    client.refreshConnectionStatus();
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Connected to TG 91"));

    QByteArray requestPayload;
    QDataStream requestStream(&requestPayload, QIODevice::WriteOnly);
    requestStream.setByteOrder(QDataStream::BigEndian);
    requestStream << quint16(Svxlink::MsgType::REQUEST_QSY);
    requestStream << quint32(3200);
    feedIncomingPayload(client, socket, requestPayload);

    QCOMPARE(qsySpy.count(), 1);
    QCOMPARE(qsySpy.at(0).at(0).toUInt(), 3200u);
    QCOMPARE(client.m_talkgroup, 3200u);
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Connected to TG 3200"));

    const QByteArray responsePayload = decodeSingleOutgoingPayload(socket);
    QVERIFY(!responsePayload.isEmpty());
    QDataStream responseStream(responsePayload);
    responseStream.setByteOrder(QDataStream::BigEndian);
    quint16 type = 0;
    quint32 talkgroup = 0;
    responseStream >> type >> talkgroup;
    QCOMPARE(type, quint16(Svxlink::MsgType::SELECT_TG));
    QCOMPARE(talkgroup, 3200u);
}

void ReflectorClientTest::nodeListFrameDecodesLengthPrefixedEntries()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);
    QSignalSpy nodesSpy(&client, &ReflectorClient::connectedNodesChanged);

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::NODE_LIST);
    stream << quint16(2);

    const QByteArray first("YO6SAY");
    stream << quint16(first.size());
    stream.writeRawData(first.constData(), first.size());

    const QByteArray second("A2");
    stream << quint16(second.size());
    stream.writeRawData(second.constData(), second.size());

    feedIncomingPayload(client, socket, payload);

    QCOMPARE(nodesSpy.count(), 1);
    QCOMPARE(nodesSpy.at(0).at(0).toStringList(),
             QStringList({QStringLiteral("YO6SAY"), QStringLiteral("A2")}));
}

void ReflectorClientTest::nodeJoinLeaveFramesDecodeLengthPrefixedCallsigns()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);
    QSignalSpy joinedSpy(&client, &ReflectorClient::nodeJoined);
    QSignalSpy leftSpy(&client, &ReflectorClient::nodeLeft);

    QByteArray joinedPayload;
    {
        QDataStream stream(&joinedPayload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << quint16(Svxlink::MsgType::NODE_JOINED);
        const QByteArray callsign("YO6SAY");
        stream << quint16(callsign.size());
        stream.writeRawData(callsign.constData(), callsign.size());
    }
    feedIncomingPayload(client, socket, joinedPayload);

    QByteArray leftPayload;
    {
        QDataStream stream(&leftPayload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << quint16(Svxlink::MsgType::NODE_LEFT);
        const QByteArray callsign("YO6SAY");
        stream << quint16(callsign.size());
        stream.writeRawData(callsign.constData(), callsign.size());
    }
    feedIncomingPayload(client, socket, leftPayload);

    QCOMPARE(joinedSpy.count(), 1);
    QCOMPARE(joinedSpy.at(0).at(0).toString(), QStringLiteral("YO6SAY"));
    QCOMPARE(leftSpy.count(), 1);
    QCOMPARE(leftSpy.at(0).at(0).toString(), QStringLiteral("YO6SAY"));
}

void ReflectorClientTest::malformedNodeFramesAreIgnoredWithoutDisconnect()
{
    ReflectorClient client;
    FakeTcpSocket *socket = installFakeTcpSocket(client);
    QSignalSpy connectionSpy(&client, &ReflectorClient::connectionStatusChanged);
    QSignalSpy nodesSpy(&client, &ReflectorClient::connectedNodesChanged);
    QSignalSpy joinedSpy(&client, &ReflectorClient::nodeJoined);
    QSignalSpy leftSpy(&client, &ReflectorClient::nodeLeft);

    client.m_state = ReflectorClient::Connected;
    client.m_connectionStatus = QStringLiteral("Connected");

    QByteArray malformedNodeList;
    {
        QDataStream stream(&malformedNodeList, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << quint16(Svxlink::MsgType::NODE_LIST);
        stream << quint16(1);
        stream << quint16(6);
        stream.writeRawData("YO6", 3);
    }
    feedIncomingPayload(client, socket, malformedNodeList);

    QByteArray malformedNodeJoined;
    {
        QDataStream stream(&malformedNodeJoined, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << quint16(Svxlink::MsgType::NODE_JOINED);
        stream << quint16(6);
        stream.writeRawData("YO6", 3);
    }
    feedIncomingPayload(client, socket, malformedNodeJoined);

    QByteArray malformedNodeLeft;
    {
        QDataStream stream(&malformedNodeLeft, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << quint16(Svxlink::MsgType::NODE_LEFT);
        stream << quint16(6);
        stream.writeRawData("YO6", 3);
    }
    feedIncomingPayload(client, socket, malformedNodeLeft);

    QCOMPARE(client.m_state, ReflectorClient::Connected);
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Connected"));
    QCOMPARE(connectionSpy.count(), 0);
    QCOMPARE(nodesSpy.count(), 0);
    QCOMPARE(joinedSpy.count(), 0);
    QCOMPARE(leftSpy.count(), 0);
}

void ReflectorClientTest::validatedNetworkLossMovesClientToWaitingState()
{
    ReflectorClient client;

    client.m_host = QStringLiteral("reflector.example");
    client.m_port = 5337;
    client.m_authKey = QByteArrayLiteral("secret");
    client.m_callsign = QStringLiteral("YO6SAY");
    client.m_talkgroup = 9;
    client.m_state = ReflectorClient::Connected;

    client.handleAndroidNetworkStateChanged(1, 1, true, true, 1, false, false, false);
    client.handleAndroidNetworkStateChanged(2, 5, false, false, 0, false, false, true);

    QCOMPARE(client.m_state, ReflectorClient::Disconnected);
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Waiting for validated network..."));
    QVERIFY(client.m_waitingForValidatedNetwork);
    QVERIFY(!client.m_reconnectTimer->isActive());
    QCOMPARE(client.m_authKey, QByteArrayLiteral("secret"));
}

void ReflectorClientTest::validatedNetworkRestorationSchedulesImmediateReconnect()
{
    ReflectorClient client;

    client.m_host = QStringLiteral("reflector.example");
    client.m_port = 5337;
    client.m_authKey = QByteArrayLiteral("secret");
    client.m_callsign = QStringLiteral("YO6SAY");
    client.m_talkgroup = 9;
    client.m_state = ReflectorClient::Disconnected;
    client.m_connectionStatus = QStringLiteral("Waiting for validated network...");
    client.m_waitingForValidatedNetwork = true;
    client.m_androidNetworkStateKnown = true;
    client.m_hasDefaultNetwork = false;
    client.m_validatedDefaultNetwork = false;

    client.handleAndroidNetworkStateChanged(3, 2, true, true, 1, false, false, true);

    QVERIFY(!client.m_waitingForValidatedNetwork);
    QVERIFY(client.m_reconnectTimer->isActive());
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Reconnecting to reflector.example..."));
}

void ReflectorClientTest::validatedRouteChangeForcesReconnect()
{
    ReflectorClient client;

    client.m_host = QStringLiteral("reflector.example");
    client.m_port = 5337;
    client.m_authKey = QByteArrayLiteral("secret");
    client.m_callsign = QStringLiteral("YO6SAY");
    client.m_talkgroup = 9;
    client.m_state = ReflectorClient::Connected;

    client.handleAndroidNetworkStateChanged(1, 1, true, true, 1, false, false, false);
    client.handleAndroidNetworkStateChanged(2, 4, true, true, 2, false, false, true);

    QCOMPARE(client.m_state, ReflectorClient::Disconnected);
    QVERIFY(client.m_reconnectTimer->isActive());
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Reconnecting to reflector.example..."));
    QCOMPARE(client.m_authKey, QByteArrayLiteral("secret"));
}

void ReflectorClientTest::inboundHeartbeatsArmProtocolLivenessWatchdog()
{
    ReflectorClient client;

    client.m_state = ReflectorClient::Connected;

    client.noteInboundProtocolHeartbeat();
    QVERIFY(!client.m_protocolLivenessTimer->isActive());

    QTest::qWait(10);
    client.noteInboundProtocolHeartbeat();

    QVERIFY(client.m_protocolLivenessTimer->isActive());
    QVERIFY(client.protocolLivenessTimeoutMs() >= 15000);
}

void ReflectorClientTest::protocolLivenessTimeoutSchedulesReconnect()
{
    ReflectorClient client;

    client.m_host = QStringLiteral("reflector.example");
    client.m_port = 5337;
    client.m_authKey = QByteArrayLiteral("secret");
    client.m_callsign = QStringLiteral("YO6SAY");
    client.m_talkgroup = 9;
    client.m_state = ReflectorClient::Connected;

    client.noteInboundProtocolHeartbeat();
    QTest::qWait(10);
    client.noteInboundProtocolHeartbeat();
    QVERIFY(client.m_protocolLivenessTimer->isActive());

    client.onProtocolLivenessTimeout();

    QCOMPARE(client.m_state, ReflectorClient::Disconnected);
    QVERIFY(client.m_reconnectTimer->isActive());
    QCOMPARE(client.m_connectionStatus, QStringLiteral("Reconnecting to reflector.example..."));
}

// ---------------------------------------------------------------------------
// Shutdown / ANR-fix tests
// ---------------------------------------------------------------------------

void ReflectorClientTest::prepareForShutdownSetsShutdownFlag()
{
    ReflectorClient client;
    QVERIFY(!client.m_shutdownComplete);

    client.prepareForShutdown();
    QVERIFY(client.m_shutdownComplete);
}

void ReflectorClientTest::prepareForShutdownIsIdempotent()
{
    ReflectorClient client;

    client.prepareForShutdown();
    QVERIFY(client.m_shutdownComplete);

    // Second call must not crash or change observable state.
    client.prepareForShutdown();
    QVERIFY(client.m_shutdownComplete);
}

void ReflectorClientTest::prepareForShutdownStopsAllTimers()
{
    ReflectorClient client;

    // Start every timer so we can verify they get stopped.
    client.m_heartbeatTimer->start(1000);
    client.m_txTimer->start(1000);
    client.m_pttHangTimer->start(1000);
    client.m_connectTimer->start(1000);
    client.m_audioTimeoutTimer->start(1000);
    client.m_talkgroupSelectionTimer->start(1000);
    client.m_transcriptionSupportRefreshTimer->start(1000);

    QVERIFY(client.m_heartbeatTimer->isActive());
    QVERIFY(client.m_txTimer->isActive());
    QVERIFY(client.m_pttHangTimer->isActive());
    QVERIFY(client.m_connectTimer->isActive());
    QVERIFY(client.m_audioTimeoutTimer->isActive());
    QVERIFY(client.m_talkgroupSelectionTimer->isActive());
    QVERIFY(client.m_transcriptionSupportRefreshTimer->isActive());

    client.prepareForShutdown();

    QVERIFY(!client.m_heartbeatTimer->isActive());
    QVERIFY(!client.m_txTimer->isActive());
    QVERIFY(!client.m_pttHangTimer->isActive());
    QVERIFY(!client.m_connectTimer->isActive());
    QVERIFY(!client.m_audioTimeoutTimer->isActive());
    QVERIFY(!client.m_talkgroupSelectionTimer->isActive());
    QVERIFY(!client.m_transcriptionSupportRefreshTimer->isActive());
}

void ReflectorClientTest::prepareForShutdownStopsAudioThread()
{
    ReflectorClient client;

    // The constructor calls initializeAudioEngine() which creates and starts
    // the audio thread.  Verify it is running before shutdown.
    QVERIFY(client.m_audioThread);
    QVERIFY(client.m_audioThread->isRunning());

    client.prepareForShutdown();

    QVERIFY(!client.m_audioThread->isRunning());
}

void ReflectorClientTest::prepareForShutdownAbortsNetworkReply()
{
    ReflectorClient client;

    // Simulate an in-flight name lookup by issuing a dummy GET.
    QNetworkRequest request(QUrl(QStringLiteral("http://localhost:1/dummy")));
    client.m_nameReply = client.m_networkManager->get(request);
    QVERIFY(client.m_nameReply != nullptr);

    client.prepareForShutdown();

    QVERIFY(client.m_nameReply == nullptr);
}

void ReflectorClientTest::destructorIsNoOpAfterPrepareForShutdown()
{
    // Allocate on the heap so we can control destruction timing.
    auto *client = new ReflectorClient;
    QVERIFY(client->m_audioThread->isRunning());

    client->prepareForShutdown();
    QVERIFY(client->m_shutdownComplete);
    QVERIFY(!client->m_audioThread->isRunning());

    // Destructor must not crash or block — the audio thread is already stopped.
    delete client;
}

void ReflectorClientTest::destructorFallbackStopsAudioThread()
{
    // When prepareForShutdown() was NOT called, the destructor must still
    // join the audio thread (with a bounded wait) as a safety net.
    auto *client = new ReflectorClient;
    QVERIFY(client->m_audioThread->isRunning());
    QVERIFY(!client->m_shutdownComplete);

    delete client;
    // If we get here without hanging, the bounded-wait fallback works.
}

void ReflectorClientTest::aboutToQuitTriggersPrepareForShutdown()
{
    ReflectorClient client;
    QVERIFY(!client.m_shutdownComplete);

    // aboutToQuit uses QPrivateSignal so we cannot emit it externally.
    // Instead verify that the constructor wired the connection by checking
    // that disconnect succeeds — it returns true only if the connection exists.
    bool wasConnected = QObject::disconnect(QCoreApplication::instance(),
                                            &QCoreApplication::aboutToQuit,
                                            &client,
                                            &ReflectorClient::prepareForShutdown);
    QVERIFY2(wasConnected, "prepareForShutdown must be connected to aboutToQuit");

    // Re-establish the connection so the client cleans up properly.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     &client, &ReflectorClient::prepareForShutdown);
}

void ReflectorClientTest::audioThreadWaitHasBoundedTimeout()
{
    // Create a thread whose event loop never processes the quit event,
    // simulating a stuck audio thread.  Verify that prepareForShutdown()
    // does not block forever (it should terminate the thread after 500 ms).
    ReflectorClient client;
    QVERIFY(client.m_audioThread->isRunning());

    // Replace the audio thread with one that ignores quit.
    QThread *stuckThread = new QThread(&client);
    stuckThread->start();

    // Install an event filter that swallows quit events.
    class QuitBlocker : public QObject {
    public:
        using QObject::QObject;
        bool eventFilter(QObject *obj, QEvent *event) override {
            if (event->type() == QEvent::Quit)
                return true;  // swallow
            return QObject::eventFilter(obj, event);
        }
    };
    auto *blocker = new QuitBlocker(stuckThread);
    stuckThread->installEventFilter(blocker);

    // Stop the real audio thread first so the destructor doesn't double-stop.
    client.m_audioThread->quit();
    client.m_audioThread->wait();

    // Swap in the stuck thread.
    QThread *originalThread = client.m_audioThread;
    client.m_audioThread = stuckThread;
    client.m_shutdownComplete = false;

    QElapsedTimer elapsed;
    elapsed.start();
    client.prepareForShutdown();
    const qint64 ms = elapsed.elapsed();

    // Must complete within a reasonable bound (500 ms wait + terminate + margin).
    QVERIFY2(ms < 3000, qPrintable(QStringLiteral("prepareForShutdown took %1 ms").arg(ms)));
    QVERIFY(!stuckThread->isRunning());

    // Restore original thread pointer so the destructor doesn't double-free.
    client.m_audioThread = originalThread;
}

QTEST_GUILESS_MAIN(ReflectorClientTest)

#include "tst_reflector_client.moc"
