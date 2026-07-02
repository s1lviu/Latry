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

#ifndef REFLECTORCLIENT_H
#define REFLECTORCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QThread>
#include <QAbstractSocket>
#include <QVariantList>
#include <QElapsedTimer>
#include "AudioEngine.h"
#ifndef LATRY_SERVICE_BUILD
#include "SppPttBridge.h"
#endif
#include <memory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QJsonObject>
#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

class QDataStream; // Forward declaration

class ReflectorClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool pttActive READ pttActive NOTIFY pttActiveChanged)
    Q_PROPERTY(QString currentTalker READ currentTalker NOTIFY currentTalkerChanged)
    Q_PROPERTY(QString currentTalkerName READ currentTalkerName NOTIFY currentTalkerNameChanged)
    Q_PROPERTY(QString txTimeString READ txTimeString NOTIFY txTimeStringChanged)
    Q_PROPERTY(bool isDisconnected READ isDisconnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool audioReady READ audioReady NOTIFY audioReadyChanged)
    Q_PROPERTY(bool isReceivingAudio READ isReceivingAudio NOTIFY isReceivingAudioChanged)
    Q_PROPERTY(quint32 selectedTalkgroup READ selectedTalkgroup NOTIFY selectedTalkgroupChanged)
    Q_PROPERTY(QVariantList monitoredTalkgroups READ monitoredTalkgroupsModel NOTIFY monitoredTalkgroupsModelChanged)
    Q_PROPERTY(QVariantList availableAudioRoutes READ availableAudioRoutes NOTIFY availableAudioRoutesChanged)
    Q_PROPERTY(QString currentAudioRoute READ currentAudioRoute NOTIFY currentAudioRouteChanged)
    Q_PROPERTY(QString preferredAudioRoute READ preferredAudioRoute NOTIFY preferredAudioRouteChanged)
    Q_PROPERTY(qreal rxAudioLevelDb READ rxAudioLevelDb NOTIFY rxAudioLevelDbChanged)
    Q_PROPERTY(qreal txAudioLevelDb READ txAudioLevelDb NOTIFY txAudioLevelDbChanged)
    Q_PROPERTY(int txTimeoutSeconds READ txTimeoutSeconds NOTIFY txTimeoutSecondsChanged)
    Q_PROPERTY(int pttHangTimeMs READ pttHangTimeMs NOTIFY pttHangTimeMsChanged)
    Q_PROPERTY(bool hardwarePttEnabled READ hardwarePttEnabled
               WRITE setHardwarePttEnabled NOTIFY hardwarePttSettingsChanged)
    Q_PROPERTY(int learnedHardwarePttKeyCode READ learnedHardwarePttKeyCode
               NOTIFY hardwarePttSettingsChanged)
    Q_PROPERTY(bool hardwarePttLearningActive READ hardwarePttLearningActive
               NOTIFY hardwarePttLearningActiveChanged)
    Q_PROPERTY(int hardwarePttLearningResult READ hardwarePttLearningResult
               NOTIFY hardwarePttLearningResultChanged)
#ifndef LATRY_SERVICE_BUILD
    Q_PROPERTY(QString learnedSppDeviceName READ learnedSppDeviceName
               NOTIFY hardwarePttSettingsChanged)
    Q_PROPERTY(QString learnedSppDeviceAddress READ learnedSppDeviceAddress
               NOTIFY hardwarePttSettingsChanged)
#endif
    Q_PROPERTY(bool txTimeoutWarning READ txTimeoutWarning NOTIFY txTimeoutWarningChanged)
    Q_PROPERTY(qreal rxMeterLevel READ rxMeterLevel NOTIFY rxMeterLevelChanged)
    Q_PROPERTY(qreal rxMeterPeakLevel READ rxMeterPeakLevel NOTIFY rxMeterPeakLevelChanged)
    Q_PROPERTY(qreal txMeterLevel READ txMeterLevel NOTIFY txMeterLevelChanged)
    Q_PROPERTY(qreal txMeterPeakLevel READ txMeterPeakLevel NOTIFY txMeterPeakLevelChanged)
    Q_PROPERTY(bool liveTranscriptionEnabled READ liveTranscriptionEnabled
               WRITE setLiveTranscriptionEnabled NOTIFY liveTranscriptionEnabledChanged)
    Q_PROPERTY(QString transcriptionText READ transcriptionText NOTIFY transcriptionTextChanged)
    Q_PROPERTY(bool transcriptionAvailable READ transcriptionAvailable
               NOTIFY transcriptionAvailabilityChanged)
    Q_PROPERTY(QVariantList transcriptionInstalledLanguages READ transcriptionInstalledLanguages
               NOTIFY transcriptionLanguageModelsChanged)
    Q_PROPERTY(QVariantList transcriptionDownloadableLanguages READ transcriptionDownloadableLanguages
               NOTIFY transcriptionLanguageModelsChanged)
    Q_PROPERTY(bool transcriptionModelDownloadAvailable READ transcriptionModelDownloadAvailable
               NOTIFY transcriptionModelDownloadStateChanged)
    Q_PROPERTY(bool transcriptionModelDownloadInProgress READ transcriptionModelDownloadInProgress
               NOTIFY transcriptionModelDownloadStateChanged)
    Q_PROPERTY(int transcriptionModelDownloadProgress READ transcriptionModelDownloadProgress
               NOTIFY transcriptionModelDownloadStateChanged)
    Q_PROPERTY(QString transcriptionModelDownloadStatus READ transcriptionModelDownloadStatus
               NOTIFY transcriptionModelDownloadStateChanged)
    Q_PROPERTY(QVariantList nodeInfoReadOnlyEntries READ nodeInfoReadOnlyEntries CONSTANT)
    Q_PROPERTY(QString softwareVersion READ softwareVersion CONSTANT)

public:
    static ReflectorClient* instance();
    explicit ReflectorClient(QObject *parent = nullptr);

    QString connectionStatus() const;
    bool pttActive() const;
    QString currentTalker() const;
    QString currentTalkerName() const;
    QString txTimeString() const;
    bool isDisconnected() const { return m_state == Disconnected; }
    bool audioReady() const { return m_audioReady; }
    bool isReceivingAudio() const { return m_isReceivingAudio; }
    quint32 selectedTalkgroup() const { return m_talkgroup; }
    QVariantList monitoredTalkgroupsModel() const;
    QVariantList availableAudioRoutes() const { return m_availableAudioRoutes; }
    QString currentAudioRoute() const { return m_currentAudioRoute; }
    QString preferredAudioRoute() const { return m_preferredAudioRoute; }
    qreal rxAudioLevelDb() const { return m_rxAudioLevelDb; }
    qreal txAudioLevelDb() const { return m_txAudioLevelDb; }
    int txTimeoutSeconds() const { return m_txTimeoutSeconds; }
    int pttHangTimeMs() const { return m_pttHangTimeMs; }
    bool hardwarePttEnabled() const { return m_hardwarePttEnabled; }
    int learnedHardwarePttKeyCode() const { return m_learnedHardwarePttKeyCode; }
    #ifndef LATRY_SERVICE_BUILD
        QString learnedSppDeviceName() const { return m_learnedSppDeviceName; }
        QString learnedSppDeviceAddress() const { return m_learnedSppDeviceAddress; }
    #endif
    bool hardwarePttLearningActive() const { return m_hardwarePttLearningActive; }
    int hardwarePttLearningResult() const { return m_hardwarePttLearningResult; }
    bool txTimeoutWarning() const { return m_txTimeoutWarning; }
    qreal rxMeterLevel() const { return m_rxMeterLevel; }
    qreal rxMeterPeakLevel() const { return m_rxMeterPeakLevel; }
    qreal txMeterLevel() const { return m_txMeterLevel; }
    qreal txMeterPeakLevel() const { return m_txMeterPeakLevel; }
    bool liveTranscriptionEnabled() const { return m_liveTranscriptionEnabled; }
    QString transcriptionText() const { return m_transcriptionText; }
    bool transcriptionAvailable() const { return m_transcriptionAvailable; }
    QVariantList transcriptionInstalledLanguages() const { return m_transcriptionInstalledLanguages; }
    QVariantList transcriptionDownloadableLanguages() const { return m_transcriptionDownloadableLanguages; }
    bool transcriptionModelDownloadAvailable() const { return m_transcriptionModelDownloadAvailable; }
    bool transcriptionModelDownloadInProgress() const { return m_transcriptionModelDownloadInProgress; }
    int transcriptionModelDownloadProgress() const { return m_transcriptionModelDownloadProgress; }
    QString transcriptionModelDownloadStatus() const { return m_transcriptionModelDownloadStatus; }
    QVariantList nodeInfoReadOnlyEntries() const;
    QString softwareVersion() const { return nodeInfoSoftwareVersion(); }

    Q_INVOKABLE void connectToServer(const QString &host, int port, const QString &authKey, const QString &callsign,
                                     quint32 talkgroup, const QString &monitoredTalkgroups,
                                     int tgSelectTimeoutSeconds = 30);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void shutdownApplication();
    Q_INVOKABLE bool voipBackgroundServiceRunning() const;
    Q_INVOKABLE void togglePtt();
    Q_INVOKABLE void pttPressed();
    Q_INVOKABLE void pttReleased();
    Q_INVOKABLE void forcePttRelease();
    Q_INVOKABLE void selectTalkgroup(quint32 talkgroup);
    Q_INVOKABLE void updateProfileConfiguration(quint32 defaultTalkgroup,
                                                const QString &monitoredTalkgroups,
                                                int tgSelectTimeoutSeconds = 30);
    Q_INVOKABLE void setPreferredAudioRoute(const QString &routeId);
    Q_INVOKABLE void setRxAudioLevelDb(qreal levelDb);
    Q_INVOKABLE void setTxAudioLevelDb(qreal levelDb);
    Q_INVOKABLE void setTxTimeoutSeconds(int seconds);
    Q_INVOKABLE void setPttHangTimeMs(int milliseconds);
    Q_INVOKABLE void setHardwarePttEnabled(bool enabled);
    Q_INVOKABLE void setLearnedHardwarePttKeyCode(int keyCode);
    Q_INVOKABLE void clearLearnedHardwarePttKeyCode();
    #ifndef LATRY_SERVICE_BUILD
        Q_INVOKABLE void clearLearnedSppDevice();
    #endif
    Q_INVOKABLE void startHardwarePttLearning();
    Q_INVOKABLE void cancelHardwarePttLearning();
    Q_INVOKABLE void setLiveTranscriptionEnabled(bool enabled);
    Q_INVOKABLE void downloadTranscriptionModel(const QString &languageTag = QString());
    Q_INVOKABLE void openTranscriptionSettings();
    Q_INVOKABLE void setCustomNodeInfoEntries(const QVariantList &entries);

    void prepareForShutdown();

#if defined(Q_OS_ANDROID)
    // Android audio focus callbacks (public for JNI access)
    static void notifyAudioFocusLost();
    static void notifyAudioFocusPaused();
    static void notifyAudioFocusGained();
    static void notifyActivityPaused();
    static void notifyActivityResumed();
    static void notifyHardwarePttLearningResult(int result, int keyCode);
    static void notifyAutoDetectedPttKeyCode(int keyCode);
    static void notifyAndroidControlEvent(int eventType);
    static void notifyAndroidNetworkStateChanged(int generation,
                                                 int reason,
                                                 bool hasDefaultNetwork,
                                                 bool validated,
                                                 int transport,
                                                 bool metered,
                                                 bool captivePortal,
                                                 bool routeChanged);
    static void notifyAndroidAudioRoutesChanged(const QString &currentRoute,
                                                const QStringList &availableRoutes);
    static void notifyPartialTranscription(const QString &text);
    static void notifyFinalTranscription(const QString &text);
    static void notifyTranscriptionError(int errorCode, const QString &message);
    static void notifyTranscriptionStopped();
#endif

private:
    friend class ReflectorClientTest;

    void startTransmission();
    void cancelPendingPttRelease();
    void beginImmediatePttRelease();
    void startSppPttBridgeIfNeeded();
    void stopTransmissionCaptureForDisconnect();
#if defined(Q_OS_ANDROID)
    bool hasAuthorizedRecordAudioPermission() const;
    void requestRecordAudioPermissionIfNeeded();
    void handleRecordAudioPermissionResult(bool authorized);
    void triggerTxTimeoutWarningHaptic();
    void resumeAndroidPttAfterReconnectIfReady();
#endif

signals:
    void connectionStatusChanged();
    void pttActiveChanged();
    void currentTalkerChanged();
    void currentTalkerNameChanged();
    void txTimeStringChanged();
    void audioReadyChanged();
    void isReceivingAudioChanged();
    void selectedTalkgroupChanged();
    void monitoredTalkgroupsModelChanged();
    void availableAudioRoutesChanged();
    void currentAudioRouteChanged();
    void preferredAudioRouteChanged();
    void rxAudioLevelDbChanged();
    void txAudioLevelDbChanged();
    void txTimeoutSecondsChanged();
    void pttHangTimeMsChanged();
    void hardwarePttSettingsChanged();
    void hardwarePttLearningActiveChanged();
    void hardwarePttLearningResultChanged();
    void txTimeoutWarningChanged();
    void rxMeterLevelChanged();
    void rxMeterPeakLevelChanged();
    void txMeterLevelChanged();
    void txMeterPeakLevelChanged();
    void liveTranscriptionEnabledChanged();
    void transcriptionTextChanged();
    void transcriptionAvailabilityChanged();
    void transcriptionLanguageModelsChanged();
    void transcriptionModelDownloadStateChanged();
    
    // New protocol signals
    void connectedNodesChanged(const QStringList &nodes);
    void nodeJoined(const QString &callsign);
    void nodeLeft(const QString &callsign);
    void monitoredTalkgroupsChanged(const QList<quint32> &talkgroups);
    void qsyRequested(quint32 newTalkgroup);
    void stateEventReceived(const QString &src, const QString &name, const QString &message);
    void signalStrengthReceived(const QString &callsign, float rxSignal, float rxSqlOpen);
    void txStatusReceived(const QString &callsign, bool isTransmitting);
    
    // Audio focus signals (Android)
    void audioFocusLost();
    void audioFocusPaused();
    void audioFocusGained();
    void activityPaused();
    void activityResumed();

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpReadyRead();
    void onUdpReadyRead();
    void onTcpError(QAbstractSocket::SocketError socketError);
    void onHeartbeatTimer();
    void onTxTimerTimeout();
    void onNameLookupFinished();
    void startNameLookup(const QString &callsign);
    void onConnectTimeout();
    void onAudioSetupFinished();
    void onAudioDataEncoded(const QByteArray &encodedData);
    void onTxDrainComplete();
    void onPttHangTimerTimeout();
    void checkAndReconnect();
    void onTalkgroupSelectionTimer();
    void onReconnectBackoffTimeout();
    void onProtocolLivenessTimeout();
    void handleAndroidNetworkStateChanged(int generation,
                                          int reason,
                                          bool hasDefaultNetwork,
                                          bool validated,
                                          int transport,
                                          bool metered,
                                          bool captivePortal,
                                          bool routeChanged);
#if defined(Q_OS_ANDROID)
    void handleAndroidControlEvent(int eventType);
    void handleAndroidAudioRoutesChanged(const QString &currentRoute,
                                         const QStringList &availableRoutes);
#endif

private:
    ~ReflectorClient();
    ReflectorClient(const ReflectorClient&) = delete;
    ReflectorClient& operator=(const ReflectorClient&) = delete;

    void sendFrame(const QByteArray &payload);
    void sendProtoVer();
    void sendAuthResponse(const QByteArray &hmac);
    void sendNodeInfo();
    void sendSelectTG(quint32 talkgroup);
    void sendTgMonitor(const QList<quint32> &talkgroups);
    void sendHeartbeat();
    void sendUdpMessage(const QByteArray &datagram);
    void sendTxFlushSamples();
    void setupAudio();
    void initializeAudioEngine();
    void refreshConnectionStatus();
    void refreshMonitoredTalkgroupsModel();
    void updateTxTimeoutWarningState();
    void transitionToDisconnectedState(const QString &status, bool preserveReconnectContext);
    void scheduleReconnectAttempt(const QString &reason, bool immediate);
    void resetReconnectBackoff();
    void clearReconnectSchedule();
    bool hasValidatedNetworkForReconnect() const;
    void setWaitingForValidatedNetwork(bool waiting);
    QString reconnectStatusText() const;
    void resetProtocolLivenessWatchdog();
    void noteInboundProtocolHeartbeat();
    int protocolLivenessTimeoutMs() const;
    static int normalizeTgSelectTimeoutSeconds(int seconds);
    static QString nodeInfoSoftwareName();
    static QString nodeInfoSoftwareVersion();
    static QString nodeInfoTipHtml();
    static QString nodeInfoWebsite();
    static bool isReservedNodeInfoKey(const QString &key);
    static QJsonObject sanitizeCustomNodeInfoEntries(const QVariantList &entries);
    void setAudioRouteState(const QString &currentRoute, const QStringList &availableRouteIds);
    void applyAudioLevelsToEngine();
    void setReceivingAudioState(bool receiving);
    void checkTranscriptionAvailability(bool androidServiceLaunch);
    void refreshTranscriptionSupportState();
    void startTranscriptionSession();
    void stopTranscriptionSession(bool clearText);
    void setTranscriptionAvailable(bool available);
    void setTranscriptionLanguageModels(const QStringList &installedLanguages,
                                       const QStringList &pendingLanguages,
                                       const QStringList &supportedLanguages,
                                       const QString &downloadTargetLanguage);
    void setTranscriptionModelDownloadState(bool available,
                                           bool inProgress,
                                           int progress,
                                           const QString &status);
    void clearTranscriptionState();
    void updateTranscriptionDisplay();
    void handlePartialTranscription(const QString &text);
    void handleFinalTranscription(const QString &text);
    void handleTranscriptionError(int errorCode, const QString &message);
    static QString normalizeTranscriptionSnippet(const QString &text);
    void setRxMeterState(qreal level, qreal peakLevel);
    void setTxMeterState(qreal level, qreal peakLevel);
    void resetAudioMeters();
    void resetTalkgroupSelectionTimer();
    void stopTalkgroupSelectionTimer();
    void clearMonitoredTalkgroups();
    quint8 monitoredTalkgroupPriority(quint32 talkgroup) const;
    bool isTalkgroupMonitored(quint32 talkgroup) const;
    bool shouldHandleTalkerStart(quint32 talkgroup);

    struct ReconnectContext {
        QString host;
        int port = 0;
        QByteArray authKey;
        QString callsign;
        quint32 talkgroup = 0;
        QString monitoredTalkgroups;
        int tgSelectTimeoutSeconds = 30;

        bool isValid() const
        {
            return !host.isEmpty() && port > 0 && !authKey.isEmpty() && !callsign.isEmpty();
        }
    };
    bool resolveReconnectContext(ReconnectContext &context);

    enum class AndroidNetworkTransport {
        Unknown = 0,
        Wifi = 1,
        Cellular = 2,
        Ethernet = 3,
        Other = 4
    };

    enum class TalkgroupSelectionOrigin {
        Manual,
        RemoteActivation,
        RemotePriorityActivation,
        RequestQsy,
        Timeout,
        TxDefaultActivation
    };
    bool selectTalkgroupInternal(quint32 talkgroup, TalkgroupSelectionOrigin origin);

    struct MonitoredTalkgroupEntry {
        quint32 talkgroup = 0;
        quint8 priority = 0;
    };

    struct ParsedMonitoredTalkgroups {
        QString normalizedSpec;
        QList<MonitoredTalkgroupEntry> configured;
        QList<quint32> activeTalkgroups;
    };

    static ParsedMonitoredTalkgroups parseMonitoredTalkgroupsSpec(const QString &monitoredTalkgroups,
                                                                  quint32 primaryTalkgroup);
    void applyMonitoredTalkgroups(const QString &monitoredTalkgroups);
    
#if defined(Q_OS_ANDROID)
    void acquireWakeLock();
    void releaseWakeLock();
    bool loadSavedAndroidReconnectProfile(QString &host, int &port, QByteArray &authKey,
                                          QString &callsign, quint32 &talkgroup,
                                          QString &monitoredTalkgroups,
                                          int &tgSelectTimeoutSeconds);
    void initializeAndroidAudioRouting();
    void stopAndroidAudioRouting();
    void ensureVoipService();
    void startVoipService(bool monitorConnection);
    void stopVoipService();
    void setServiceConnectionMonitoring(bool enabled);
    void updateServiceConnectionStatus(const QString& status, bool connected);
    void updateServiceCurrentTalker(const QString& talker);
    void updateServiceSelectedTalkgroup(quint32 talkgroup);
    void updateServiceReceiveState(bool receiving, const QString& talker);
    void updateServiceTransmitState(bool transmitting);
    void saveConnectionState();
    void clearConnectionState();
    void refreshHardwarePttSettings();
#endif

    // --- Private Helper Functions for Message Handling ---
    void handleAuthChallenge(QDataStream &stream);
    void handleServerInfo(QDataStream &stream);
    void handleTalkerStart(QDataStream &stream);
    void handleTalkerStop(QDataStream &stream);
    void handleTalkerStartV1(QDataStream &stream);
    void handleTalkerStopV1(QDataStream &stream);

    enum State {
        Disconnected,
        Connecting,
        Authenticating,
        Connected
    };

    State m_state = Disconnected;
    QString m_connectionStatus = "Disconnected";
    bool m_pttActive = false;
    bool m_txStopPending = false;
    bool m_audioReady = false;

    QTcpSocket* m_tcpSocket = nullptr;
    QUdpSocket* m_udpSocket = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
    QByteArray m_tcpBuffer;
    QString m_host;
    int m_port;
    QByteArray m_authKey;
    QString m_callsign;
    quint32 m_talkgroup;
    quint32 m_defaultTalkgroup;
    quint16 m_clientId = 0;
    uint16_t m_udpSequence = 0;

    // Audio engine and thread
    AudioEngine* m_audioEngine = nullptr;
    QThread* m_audioThread = nullptr;

    QString m_currentTalker;
    quint16 m_lastAudioSeq = 0;
    bool m_isReceivingAudio = false;
    QVariantList m_availableAudioRoutes;
    QString m_currentAudioRoute;
    QString m_preferredAudioRoute;
    qreal m_rxAudioLevelDb = 0.0;
    qreal m_txAudioLevelDb = 0.0;
    bool m_hardwarePttEnabled = false;
    int m_learnedHardwarePttKeyCode = -1;
    bool m_hardwarePttLearningActive = false;
    int m_hardwarePttLearningResult = 0;
    QString m_learnedSppDeviceName;
    QString m_learnedSppDeviceAddress;
    #ifndef LATRY_SERVICE_BUILD
        std::unique_ptr<SppPttBridge> m_sppPttBridge;
    #endif
    qreal m_rxMeterLevel = 0.0;
    qreal m_rxMeterPeakLevel = 0.0;
    qreal m_txMeterLevel = 0.0;
    qreal m_txMeterPeakLevel = 0.0;
    bool m_liveTranscriptionEnabled = false;
    QString m_transcriptionText;
    bool m_transcriptionAvailable = false;
    QVariantList m_transcriptionInstalledLanguages;
    QVariantList m_transcriptionDownloadableLanguages;
    bool m_transcriptionModelDownloadAvailable = false;
    bool m_transcriptionModelDownloadInProgress = false;
    int m_transcriptionModelDownloadProgress = -1;
    QString m_transcriptionModelDownloadStatus;
    bool m_transcriptionSessionActive = false;
    bool m_recordAudioPermissionRequestPending = false;
    bool m_transcriptionPermissionEnablePending = false;
    bool m_transcriptionSuspendedByPause = false;
    QString m_transcriptionCommittedText;
    QString m_transcriptionPendingText;
    QString m_lastFinalTranscriptionSegment;
    QTimer* m_transcriptionSupportRefreshTimer = nullptr;
    QList<quint32> m_monitoredTalkgroups;
    QList<MonitoredTalkgroupEntry> m_configuredMonitoredTalkgroups;
    QString m_monitoredTalkgroupsSpec;
    QJsonObject m_customNodeInfoJson;
    QTimer* m_audioTimeoutTimer = nullptr;
    QTimer* m_talkgroupSelectionTimer = nullptr;
    int m_tgSelectTimeoutCounter = 0;
    int m_tgSelectTimeoutSeconds = 30;
    bool m_usePriorityMode = true;

    QTimer* m_txTimer = nullptr;
    QTimer* m_pttHangTimer = nullptr;
    QTimer* m_connectTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_protocolLivenessTimer = nullptr;
    bool m_txTimeoutEnabled = true;
    int m_txTimeoutSeconds = 175;
    int m_pttHangTimeMs = 100;
    int m_txSeconds = 0;
    bool m_pttReleasePending = false;
    bool m_pttPermissionRestartPending = false;
#if defined(Q_OS_ANDROID)
    bool m_resumeAndroidPttAfterReconnect = false;
#endif
    bool m_txTimeoutWarning = false;
    bool m_txTimeoutWarningFeedbackSent = false;

    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_nameReply = nullptr;
    QString m_currentTalkerName;
    bool m_androidNetworkStateKnown = false;
    bool m_hasDefaultNetwork = false;
    bool m_validatedDefaultNetwork = false;
    AndroidNetworkTransport m_androidNetworkTransport = AndroidNetworkTransport::Unknown;
    bool m_androidNetworkMetered = false;
    bool m_androidNetworkCaptivePortal = false;
    int m_lastAndroidNetworkGeneration = 0;
    bool m_waitingForValidatedNetwork = false;
    int m_reconnectBackoffStep = 0;
    bool m_ignoreNextSocketDisconnect = false;
    bool m_ignoreNextSocketError = false;
    QElapsedTimer m_protocolHeartbeatClock;
    qint64 m_lastInboundHeartbeatMs = -1;
    QList<qint64> m_recentInboundHeartbeatIntervals;
    bool m_shutdownComplete = false;
};

#endif // REFLECTORCLIENT_H
