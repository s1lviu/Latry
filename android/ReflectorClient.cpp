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
#include "AppLaunchMode.h"
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <QDebug>
#include <QTime>
#include <QTimer>
#include <QSet>
#include <QVariantMap>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <algorithm>
#include <optional>

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QFuture>
#  include <QFutureWatcher>
#  include <unistd.h>
namespace AndroidReflectorClientJniInterop {
void drainPendingReflectorActions(ReflectorClient *client);
}
#endif

namespace {
const QString kAudioRouteSpeaker = QStringLiteral("speaker");
const QString kAudioRouteWiredHeadset = QStringLiteral("wired_headset");
const QString kAudioRouteBluetooth = QStringLiteral("bluetooth");
const QString kAudioRouteBluetoothPrefix = QStringLiteral("bluetooth:");

bool isBluetoothRouteId(const QString &routeId)
{
    return routeId == kAudioRouteBluetooth
           || routeId.startsWith(kAudioRouteBluetoothPrefix);
}

constexpr qreal kMinRxAudioLevelDb = 0.0;
constexpr qreal kMaxRxAudioLevelDb = 9.0;
constexpr qreal kMinTxAudioLevelDb = -12.0;
constexpr qreal kMaxTxAudioLevelDb = 12.0;
constexpr int kDefaultTgSelectTimeoutSeconds = 30;
constexpr int kMinTgSelectTimeoutSeconds = 1;
constexpr bool kDefaultTxTimeoutEnabled = true;
constexpr int kDefaultTxTimeoutSeconds = 175;
constexpr int kDefaultPttHangTimeMs = 100;
constexpr int kMaxPttHangTimeMs = 1000;
constexpr int kNoLearnedHardwarePttKeyCode = -1;
constexpr int kTxTimeoutWarningWindowSeconds = 10;
constexpr int kMaxTranscriptionChars = 1200;
constexpr int kAndroidSpeechErrorLanguageNotSupported = 12;
constexpr int kAndroidSpeechErrorLanguageUnavailable = 13;
constexpr int kTranscriptionSupportRefreshIntervalMs = 2000;
#if defined(Q_OS_ANDROID)
const QString kRecordAudioPermission = QStringLiteral("android.permission.RECORD_AUDIO");

QJniObject androidQtContext()
{
    return QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
}

std::optional<QtAndroidPrivate::PermissionResult> permissionResultFromFuture(
    const QFuture<QtAndroidPrivate::PermissionResult> &future,
    const char *operation)
{
    if (!future.isValid()) {
        qWarning() << operation << "returned an invalid permission future";
        return std::nullopt;
    }

    if (future.isCanceled()) {
        qWarning() << operation << "was canceled before Android returned a result";
        return std::nullopt;
    }

    if (future.resultCount() < 1) {
        qWarning() << operation << "finished without a permission result";
        return std::nullopt;
    }

    return future.resultAt(0);
}
#endif

QString normalizeAudioRouteId(const QString &routeId)
{
    const QString normalized = routeId.trimmed().toLower();
    if (normalized == kAudioRouteWiredHeadset) {
        return normalized;
    }
    if (isBluetoothRouteId(normalized)) {
        return normalized;
    }
    return kAudioRouteSpeaker;
}

qreal normalizeRxAudioLevelDb(qreal levelDb)
{
    return std::clamp(levelDb, kMinRxAudioLevelDb, kMaxRxAudioLevelDb);
}

qreal normalizeTxAudioLevelDb(qreal levelDb)
{
    return std::clamp(levelDb, kMinTxAudioLevelDb, kMaxTxAudioLevelDb);
}

qreal normalizeMeterLevel(qreal level)
{
    return std::clamp(level, 0.0, 1.0);
}

int normalizeTxTimeoutSeconds(int seconds)
{
    return seconds > 0 ? seconds : kDefaultTxTimeoutSeconds;
}

int normalizePttHangTimeMs(int milliseconds)
{
    if (milliseconds < 0) {
        return kDefaultPttHangTimeMs;
    }

    return std::min(milliseconds, kMaxPttHangTimeMs);
}

int normalizeTalkgroupSelectTimeoutSeconds(int seconds)
{
    return seconds >= kMinTgSelectTimeoutSeconds ? seconds : kDefaultTgSelectTimeoutSeconds;
}

QVariantMap describeAudioRoute(const QString &routeId)
{
    const QString normalized = normalizeAudioRouteId(routeId);
    if (normalized == kAudioRouteWiredHeadset) {
        return {
            {QStringLiteral("id"), normalized},
            {QStringLiteral("name"), QStringLiteral("Wired Headset")},
            {QStringLiteral("description"), QStringLiteral("Wired audio route")}
        };
    }

    if (isBluetoothRouteId(normalized)) {
        const QString deviceName = normalized.startsWith(kAudioRouteBluetoothPrefix)
                ? normalized.mid(kAudioRouteBluetoothPrefix.length())
                : QStringLiteral("Bluetooth");
        return {
            {QStringLiteral("id"), normalized},
            {QStringLiteral("name"), deviceName},
            {QStringLiteral("description"), QStringLiteral("Bluetooth voice route")}
        };
    }

    return {
        {QStringLiteral("id"), kAudioRouteSpeaker},
        {QStringLiteral("name"), QStringLiteral("Speaker")},
        {QStringLiteral("description"), QStringLiteral("Speaker + Internal mic")}
    };
}

QStringList normalizedAudioRouteIds(const QStringList &routeIds)
{
    QStringList ordered;
    ordered.append(kAudioRouteSpeaker);

    bool hasWired = false;
    QStringList bluetoothRoutes;

    for (const QString &routeId : routeIds) {
        const QString normalized = normalizeAudioRouteId(routeId);
        if (normalized == kAudioRouteWiredHeadset) {
            hasWired = true;
        } else if (isBluetoothRouteId(normalized)
                   && !bluetoothRoutes.contains(normalized)) {
            bluetoothRoutes.append(normalized);
        }
    }

    if (hasWired) {
        ordered.append(kAudioRouteWiredHeadset);
    }
    ordered.append(bluetoothRoutes);

    return ordered;
}

QVariantList buildAudioRouteModel(const QStringList &routeIds)
{
    QVariantList routes;
    const QStringList ordered = normalizedAudioRouteIds(routeIds);
    routes.reserve(ordered.size());
    for (const QString &routeId : ordered) {
        routes.append(describeAudioRoute(routeId));
    }
    return routes;
}

QVariantMap describeNodeInfoEntry(const QString &key, const QString &value)
{
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("value"), value}
    };
}

QString trimTranscriptionTail(const QString &text)
{
    if (text.size() <= kMaxTranscriptionChars) {
        return text;
    }

    return text.right(kMaxTranscriptionChars).trimmed();
}

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList strings;
    const QJsonArray array = value.toArray();
    strings.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty() && !strings.contains(text)) {
            strings.append(text);
        }
    }
    return strings;
}

QString displayLanguageName(const QString &languageTag)
{
    const QLocale locale(languageTag);
    QString languageName;
    if (locale.language() != QLocale::C) {
        languageName = QLocale::languageToString(locale.language());
    }
    if (languageName.isEmpty()) {
        languageName = languageTag;
    }

    QString territoryName;
    if (locale.territory() != QLocale::AnyTerritory) {
        territoryName = QLocale::territoryToString(locale.territory());
    }

    if (!territoryName.isEmpty()) {
        return QStringLiteral("%1 (%2)").arg(languageName, territoryName);
    }

    return languageName;
}

QVariantMap buildTranscriptionLanguageEntry(const QString &languageTag,
                                            bool installed,
                                            bool pending,
                                            bool activeDownload)
{
    QString status;
    if (installed) {
        status = QStringLiteral("Installed");
    } else if (pending || activeDownload) {
        status = QStringLiteral("Download in progress");
    } else {
        status = QStringLiteral("Available to download");
    }

    return {
        {QStringLiteral("tag"), languageTag},
        {QStringLiteral("name"), displayLanguageName(languageTag)},
        {QStringLiteral("status"), status},
        {QStringLiteral("installed"), installed},
        {QStringLiteral("pending"), pending},
        {QStringLiteral("activeDownload"), activeDownload}
    };
}
}

ReflectorClient* ReflectorClient::instance()
{
    // Ensure QCoreApplication exists before creating Qt objects
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        qWarning() << "ReflectorClient::instance() called before QCoreApplication created";
        return nullptr;
    }

    static ReflectorClient *client = nullptr;
    if (client) {
        return client;
    }

    if (QThread::currentThread() != app->thread()) {
        qCritical() << "ReflectorClient singleton must be created on the Qt application thread";
        return nullptr;
    }

    static ReflectorClient singleton;
    client = &singleton;
#if defined(Q_OS_ANDROID)
    AndroidReflectorClientJniInterop::drainPendingReflectorActions(client);
#endif
    return client;
}

ReflectorClient::ReflectorClient(QObject *parent) : QObject{parent},
    m_state(Disconnected),
    m_connectionStatus("Disconnected"),
    m_pttActive(false),
    m_txStopPending(false),
    m_audioReady(false),
    m_port(0),
    m_talkgroup(0),
    m_defaultTalkgroup(0),
    m_clientId(0),
    m_udpSequence(0),
    m_txSeconds(0),
    m_lastAudioSeq(0),
    m_currentTalker(""),
    m_currentTalkerName(""),
    m_isReceivingAudio(false)
{
    m_tcpSocket = new QTcpSocket(this);
    m_udpSocket = new QUdpSocket(this);
    m_heartbeatTimer = new QTimer(this);
    m_txTimer = new QTimer(this);
    m_pttHangTimer = new QTimer(this);
    m_connectTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    m_protocolLivenessTimer = new QTimer(this);
    m_pttHangTimer->setSingleShot(true);
    m_connectTimer->setSingleShot(true);
    m_reconnectTimer->setSingleShot(true);
    m_protocolLivenessTimer->setSingleShot(true);
    m_audioTimeoutTimer = new QTimer(this);
    m_audioTimeoutTimer->setSingleShot(true);
    m_audioTimeoutTimer->setInterval(3000); // 3 second timeout
    m_transcriptionSupportRefreshTimer = new QTimer(this);
    m_transcriptionSupportRefreshTimer->setSingleShot(false);
    m_transcriptionSupportRefreshTimer->setInterval(kTranscriptionSupportRefreshIntervalMs);
    m_talkgroupSelectionTimer = new QTimer(this);
    m_talkgroupSelectionTimer->setInterval(1000);
    m_networkManager = new QNetworkAccessManager(this);

    connect(m_tcpSocket, &QTcpSocket::connected, this, &ReflectorClient::onTcpConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ReflectorClient::onTcpDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ReflectorClient::onTcpReadyRead);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &ReflectorClient::onUdpReadyRead);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ReflectorClient::onHeartbeatTimer);
    connect(m_txTimer, &QTimer::timeout, this, &ReflectorClient::onTxTimerTimeout);
    connect(m_pttHangTimer, &QTimer::timeout, this, &ReflectorClient::onPttHangTimerTimeout);
    connect(m_connectTimer, &QTimer::timeout, this, &ReflectorClient::onConnectTimeout);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ReflectorClient::onReconnectBackoffTimeout);
    connect(m_protocolLivenessTimer, &QTimer::timeout, this, &ReflectorClient::onProtocolLivenessTimeout);
    connect(m_talkgroupSelectionTimer, &QTimer::timeout, this, &ReflectorClient::onTalkgroupSelectionTimer);
    connect(m_transcriptionSupportRefreshTimer, &QTimer::timeout,
            this, &ReflectorClient::refreshTranscriptionSupportState);
    connect(m_audioTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_isReceivingAudio) {
            qDebug() << "Audio timeout - stopping receive indicator";
            setReceivingAudioState(false);
        }
    });
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &ReflectorClient::onTcpError);
    m_txTimer->setInterval(1000);
    m_txTimeoutEnabled = kDefaultTxTimeoutEnabled;
    m_txTimeoutSeconds = normalizeTxTimeoutSeconds(kDefaultTxTimeoutSeconds);
    m_pttHangTimeMs = normalizePttHangTimeMs(kDefaultPttHangTimeMs);

    // Initialize audio engine and thread
    initializeAudioEngine();

    m_availableAudioRoutes = buildAudioRouteModel(QStringList{kAudioRouteSpeaker});
    m_currentAudioRoute = kAudioRouteSpeaker;
    m_preferredAudioRoute = kAudioRouteSpeaker;

#if defined(Q_OS_ANDROID)
    refreshHardwarePttSettings();
#endif

    const bool androidServiceLaunch = isAndroidServiceLaunchMode(QCoreApplication::arguments());
    checkTranscriptionAvailability(androidServiceLaunch);

    connect(this, &ReflectorClient::activityPaused, this, [this]() {
#if defined(Q_OS_ANDROID)
        if (!m_transcriptionSessionActive) {
            return;
        }

        m_transcriptionSuspendedByPause = true;
        stopTranscriptionSession(false);
#endif
    });
    connect(this, &ReflectorClient::activityResumed, this, [this]() {
#if defined(Q_OS_ANDROID)
        const bool shouldResume = m_transcriptionSuspendedByPause
                && m_liveTranscriptionEnabled
                && m_isReceivingAudio;
        m_transcriptionSuspendedByPause = false;
        if (shouldResume) {
            startTranscriptionSession();
        }
#endif
    });

#if defined(Q_OS_ANDROID)
    QTimer::singleShot(0, this, [this, androidServiceLaunch]() {
        initializeAndroidAudioRouting();
        if (!androidServiceLaunch) {
            ensureVoipService();
            return;
        }

        qDebug() << "ReflectorClient running inside Android service launch mode";
    });
#endif

    if (!androidServiceLaunch) {
        // Ensure UI gets the initial state values after the QML engine is ready.
        QTimer::singleShot(100, this, [this]() {
            qDebug() << "Emitting initial signals:";
            qDebug() << "  connectionStatus:" << m_connectionStatus;
            qDebug() << "  pttActive:" << m_pttActive;
            qDebug() << "  currentTalker:" << m_currentTalker;
            qDebug() << "  currentTalkerName:" << m_currentTalkerName;
            qDebug() << "  txTimeString:" << txTimeString();
            qDebug() << "  audioReady:" << m_audioReady;
            qDebug() << "  isDisconnected:" << (m_state == Disconnected);

            emit connectionStatusChanged();
            emit pttActiveChanged();
            emit currentTalkerChanged();
            emit currentTalkerNameChanged();
            emit txTimeStringChanged();
            emit audioReadyChanged();
            emit availableAudioRoutesChanged();
            emit currentAudioRouteChanged();
            emit preferredAudioRouteChanged();
            emit rxAudioLevelDbChanged();
            emit txAudioLevelDbChanged();
            emit hardwarePttSettingsChanged();
            emit hardwarePttLearningActiveChanged();
            emit hardwarePttLearningResultChanged();
            emit rxMeterLevelChanged();
            emit rxMeterPeakLevelChanged();
            emit txMeterLevelChanged();
            emit txMeterPeakLevelChanged();
        });
    }

    // Run heavy cleanup while the event loop is still alive, before dlclose()
    // triggers static destruction with the Android main thread already blocked.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &ReflectorClient::prepareForShutdown);
}

ReflectorClient::~ReflectorClient()
{
    if (!m_shutdownComplete) {
        // Fallback: prepareForShutdown() was not called (abnormal shutdown path).
        // This runs during static destruction / dlclose where the Qt event loop
        // and JNI environment may already be torn down, so only do the minimum:
        // join the audio thread with a bounded wait to avoid an infinite block.
        qWarning() << "ReflectorClient::~ReflectorClient - prepareForShutdown was not called,"
                       " performing fallback cleanup";
        if (m_audioThread && m_audioThread->isRunning()) {
            m_audioThread->quit();
            if (!m_audioThread->wait(2000))
                m_audioThread->terminate();
        }
    }
}

void ReflectorClient::prepareForShutdown()
{
    if (m_shutdownComplete)
        return;
    m_shutdownComplete = true;

    qInfo() << "ReflectorClient::prepareForShutdown - cleaning up while event loop is alive";

#if defined(Q_OS_ANDROID)
    // Mark transcription inactive without the BlockingQueuedConnection to the
    // audio thread — the thread is about to be terminated, so synchronously
    // closing the pipe fd is unnecessary and risks blocking for too long.
    m_transcriptionSessionActive = false;
    QJniObject::callStaticMethod<void>("yo6say/latry/LatryTranscriptionManager",
                                       "destroy",
                                       "()V");
#endif

    // Stop all timers to prevent new work from being scheduled.
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_txTimer) {
        m_txTimer->stop();
        m_txTimeoutWarningFeedbackSent = false;
        m_txSeconds = 0;
    }
    if (m_pttHangTimer)
        m_pttHangTimer->stop();
    if (m_connectTimer)
        m_connectTimer->stop();
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_protocolLivenessTimer)
        m_protocolLivenessTimer->stop();
    if (m_audioTimeoutTimer)
        m_audioTimeoutTimer->stop();
    if (m_talkgroupSelectionTimer)
        m_talkgroupSelectionTimer->stop();
    if (m_transcriptionSupportRefreshTimer)
        m_transcriptionSupportRefreshTimer->stop();

    // Abort pending network reply.
    if (m_nameReply) {
        m_nameReply->abort();
        m_nameReply->deleteLater();
        m_nameReply = nullptr;
    }

    // Shut down the audio thread with a tight timeout. Background ANR threshold
    // on Android 14+ is ~5 s; keep the total prepareForShutdown() time well
    // under that. If the thread cannot exit in 500 ms it is stuck — terminate it.
    // Per Qt docs: "Use QThread::wait() after terminate(), to be sure."
    if (m_audioThread && m_audioThread->isRunning()) {
        m_audioThread->quit();
        if (!m_audioThread->wait(500)) {
            qWarning() << "ReflectorClient::prepareForShutdown - audio thread did not exit"
                           " within 500 ms, terminating";
            m_audioThread->terminate();
            m_audioThread->wait(200);
        }
    }

#if defined(Q_OS_ANDROID)
    stopAndroidAudioRouting();
    stopVoipService();
    clearConnectionState();
#endif

    qInfo() << "ReflectorClient::prepareForShutdown - cleanup complete";
}

// --- Property Getters ---

QString ReflectorClient::connectionStatus() const { return m_connectionStatus; }
bool ReflectorClient::pttActive() const { return m_pttActive; }
QString ReflectorClient::currentTalker() const { return m_currentTalker; }
QVariantList ReflectorClient::monitoredTalkgroupsModel() const
{
    QVariantList talkgroups;
    talkgroups.reserve(m_monitoredTalkgroups.size());
    for (quint32 talkgroup : m_monitoredTalkgroups) {
        talkgroups.append(talkgroup);
    }
    return talkgroups;
}

QVariantList ReflectorClient::nodeInfoReadOnlyEntries() const
{
    return {
        describeNodeInfoEntry(QStringLiteral("sw"), nodeInfoSoftwareName()),
        describeNodeInfoEntry(QStringLiteral("swVer"), nodeInfoSoftwareVersion()),
        describeNodeInfoEntry(QStringLiteral("tip"), nodeInfoTipHtml()),
        describeNodeInfoEntry(QStringLiteral("Website"), nodeInfoWebsite())
    };
}

void ReflectorClient::shutdownApplication()
{
    if (m_pttActive) {
        forcePttRelease();
    }

    disconnectFromServer();
    prepareForShutdown();

#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/LatryActivity",
        "requestFinishAndRemoveTask",
        "()V");
#endif

    QCoreApplication::quit();
}

bool ReflectorClient::voipBackgroundServiceRunning() const
{
#if defined(Q_OS_ANDROID)
    return QJniObject::callStaticMethod<jboolean>(
        "yo6say/latry/VoipBackgroundService",
        "isRunning",
        "()Z");
#else
    return false;
#endif
}

QString ReflectorClient::currentTalkerName() const
{
    return m_currentTalkerName;
}

QString ReflectorClient::txTimeString() const
{
    QTime t(0, 0);
    t = t.addSecs(m_txSeconds);
    if (t.hour() > 0)
        return t.toString("h:mm:ss");
    else
        return t.toString("mm:ss");
}

void ReflectorClient::onTxTimerTimeout()
{
    ++m_txSeconds;
    emit txTimeStringChanged();
    updateTxTimeoutWarningState();

    if (m_txTimeoutWarning && !m_txTimeoutWarningFeedbackSent) {
        m_txTimeoutWarningFeedbackSent = true;
#if defined(Q_OS_ANDROID)
        triggerTxTimeoutWarningHaptic();
#endif
    }

    if (!m_txTimeoutEnabled || !m_pttActive || m_txStopPending || m_txSeconds < m_txTimeoutSeconds) {
        return;
    }

    qWarning() << "TX timeout reached after" << m_txTimeoutSeconds
               << "seconds, releasing PTT";
    forcePttRelease();
}

void ReflectorClient::selectTalkgroup(quint32 talkgroup)
{
    selectTalkgroupInternal(talkgroup, TalkgroupSelectionOrigin::Manual);
}

void ReflectorClient::updateProfileConfiguration(quint32 defaultTalkgroup,
                                                 const QString &monitoredTalkgroups,
                                                 int tgSelectTimeoutSeconds)
{
    m_defaultTalkgroup = defaultTalkgroup;
    m_tgSelectTimeoutSeconds = normalizeTgSelectTimeoutSeconds(tgSelectTimeoutSeconds);
    applyMonitoredTalkgroups(monitoredTalkgroups);

    if (m_state == Connected) {
        sendTgMonitor(m_monitoredTalkgroups);
        if (m_talkgroup > 0) {
            resetTalkgroupSelectionTimer();
        } else {
            stopTalkgroupSelectionTimer();
        }
        refreshConnectionStatus();
#if defined(Q_OS_ANDROID)
        saveConnectionState();
#endif
    }
}

int ReflectorClient::normalizeTgSelectTimeoutSeconds(int seconds)
{
    return normalizeTalkgroupSelectTimeoutSeconds(seconds);
}

QString ReflectorClient::nodeInfoSoftwareName()
{
    return QStringLiteral("Latry");
}

QString ReflectorClient::nodeInfoSoftwareVersion()
{
    return QStringLiteral(LATRY_VERSION_NAME);
}

QString ReflectorClient::nodeInfoTipHtml()
{
    return QStringLiteral("I'm using <a href=\"https://latry.app\" target=\"_blank\">Latry.app</a>");
}

QString ReflectorClient::nodeInfoWebsite()
{
    return QStringLiteral("https://latry.app");
}

bool ReflectorClient::isReservedNodeInfoKey(const QString &key)
{
    static const QSet<QString> reservedKeys = {
        QStringLiteral("sw"),
        QStringLiteral("swver"),
        QStringLiteral("tip"),
        QStringLiteral("website"),
        QStringLiteral("callsign")
    };
    return reservedKeys.contains(key.trimmed().toLower());
}

QJsonObject ReflectorClient::sanitizeCustomNodeInfoEntries(const QVariantList &entries)
{
    QJsonObject normalizedEntries;

    for (const QVariant &entryVariant : entries) {
        const QVariantMap entry = entryVariant.toMap();
        const QString key = entry.value(QStringLiteral("key")).toString().trimmed();
        const QString value = entry.value(QStringLiteral("value")).toString().trimmed();
        if (key.isEmpty() || value.isEmpty() || isReservedNodeInfoKey(key)) {
            continue;
        }
        normalizedEntries.insert(key, QJsonValue(value));
    }

    return normalizedEntries;
}

void ReflectorClient::setCustomNodeInfoEntries(const QVariantList &entries)
{
    const QJsonObject sanitizedEntries = sanitizeCustomNodeInfoEntries(entries);
    if (m_customNodeInfoJson == sanitizedEntries) {
        return;
    }

    m_customNodeInfoJson = sanitizedEntries;

    if (m_state == Connected) {
        sendNodeInfo();
    }
}

void ReflectorClient::setPreferredAudioRoute(const QString &routeId)
{
    const QString normalizedRoute = normalizeAudioRouteId(routeId);
    if (m_preferredAudioRoute != normalizedRoute) {
        m_preferredAudioRoute = normalizedRoute;
        emit preferredAudioRouteChanged();
    }

#if defined(Q_OS_ANDROID)
    initializeAndroidAudioRouting();

    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: Failed to get Android context for audio route selection";
        return;
    }

    const QJniObject routeString = QJniObject::fromString(normalizedRoute);
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/LatryAudioRouteManager",
        "setPreferredRoute",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object(),
        routeString.object());
#else
    setAudioRouteState(normalizedRoute, QStringList{normalizedRoute});
#endif
}

void ReflectorClient::setRxAudioLevelDb(qreal levelDb)
{
    const qreal normalizedLevel = normalizeRxAudioLevelDb(levelDb);
    if (m_rxAudioLevelDb != normalizedLevel) {
        m_rxAudioLevelDb = normalizedLevel;
        emit rxAudioLevelDbChanged();
    }

    applyAudioLevelsToEngine();
}

void ReflectorClient::setTxAudioLevelDb(qreal levelDb)
{
    const qreal normalizedLevel = normalizeTxAudioLevelDb(levelDb);
    if (m_txAudioLevelDb != normalizedLevel) {
        m_txAudioLevelDb = normalizedLevel;
        emit txAudioLevelDbChanged();
    }

    applyAudioLevelsToEngine();
}

void ReflectorClient::setTxTimeoutSeconds(int seconds)
{
    const int normalizedSeconds = normalizeTxTimeoutSeconds(seconds);
    if (m_txTimeoutSeconds != normalizedSeconds) {
        m_txTimeoutSeconds = normalizedSeconds;
        emit txTimeoutSecondsChanged();
    }

    updateTxTimeoutWarningState();

    if (m_pttActive && !m_txStopPending && m_txSeconds >= m_txTimeoutSeconds) {
        forcePttRelease();
    }
}

void ReflectorClient::setPttHangTimeMs(int milliseconds)
{
    const int normalizedMilliseconds = normalizePttHangTimeMs(milliseconds);
    if (m_pttHangTimeMs == normalizedMilliseconds) {
        return;
    }

    m_pttHangTimeMs = normalizedMilliseconds;
    emit pttHangTimeMsChanged();

    if (m_pttReleasePending) {
        if (m_pttHangTimeMs == 0) {
            forcePttRelease();
        } else if (m_pttHangTimer) {
            m_pttHangTimer->start(m_pttHangTimeMs);
        }
    }
}

void ReflectorClient::setHardwarePttEnabled(bool enabled)
{
    if (m_hardwarePttEnabled == enabled) {
        return;
    }

    m_hardwarePttEnabled = enabled;
    emit hardwarePttSettingsChanged();

#if defined(Q_OS_ANDROID)
    const QJniObject context = androidQtContext();
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: Failed to get Android context for hardware PTT settings";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "yo6say/latry/HardwarePttSettingsStore",
        "setPocButtonEnabled",
        "(Landroid/content/Context;Z)V",
        context.object(),
        static_cast<jboolean>(enabled));
#endif
}

void ReflectorClient::setLearnedHardwarePttKeyCode(int keyCode)
{
    const int normalizedKeyCode = keyCode > 0 ? keyCode : kNoLearnedHardwarePttKeyCode;
    if (m_learnedHardwarePttKeyCode == normalizedKeyCode) {
        return;
    }

    m_learnedHardwarePttKeyCode = normalizedKeyCode;
    emit hardwarePttSettingsChanged();

#if defined(Q_OS_ANDROID)
    const QJniObject context = androidQtContext();
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: Failed to get Android context for learned hardware PTT key";
        return;
    }

    if (normalizedKeyCode == kNoLearnedHardwarePttKeyCode) {
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/HardwarePttSettingsStore",
            "clearLearnedPttKeyCode",
            "(Landroid/content/Context;)V",
            context.object());
        return;
    }

    QJniObject::callStaticMethod<void>(
        "yo6say/latry/HardwarePttSettingsStore",
        "setLearnedPttKeyCode",
        "(Landroid/content/Context;I)V",
        context.object(),
        static_cast<jint>(normalizedKeyCode));
#endif
}

void ReflectorClient::clearLearnedHardwarePttKeyCode()
{
    setLearnedHardwarePttKeyCode(kNoLearnedHardwarePttKeyCode);
}

void ReflectorClient::startHardwarePttLearning()
{
    if (m_hardwarePttLearningActive) {
        return;
    }

#if defined(Q_OS_ANDROID)
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    const jboolean started = QJniObject::callStaticMethod<jboolean>(
        "yo6say/latry/HardwarePttLearningCoordinator",
        "startLearning",
        "(Landroid/content/Context;)Z",
        activity.object<jobject>());
    if (!started) {
        qWarning() << "ReflectorClient: Failed to start hardware PTT learning";
        return;
    }
#endif

    m_hardwarePttLearningActive = true;
    m_hardwarePttLearningResult = 0; // RESULT_NONE
    emit hardwarePttLearningActiveChanged();
    emit hardwarePttLearningResultChanged();
}

void ReflectorClient::cancelHardwarePttLearning()
{
    if (!m_hardwarePttLearningActive) {
        return;
    }

#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/HardwarePttLearningCoordinator",
        "cancelLearning",
        "()V");
#endif
    // The native callback (notifyHardwarePttLearningResult) will update state
}

void ReflectorClient::setLiveTranscriptionEnabled(bool enabled)
{
    if (!enabled) {
        m_transcriptionPermissionEnablePending = false;
    }

#if defined(Q_OS_ANDROID)
    if (enabled) {
        if (!m_transcriptionAvailable) {
            m_transcriptionPermissionEnablePending = false;
            return;
        }

        if (!hasAuthorizedRecordAudioPermission()) {
            m_transcriptionPermissionEnablePending = true;
            requestRecordAudioPermissionIfNeeded();
            return;
        }
    }
#else
    Q_UNUSED(enabled)
    return;
#endif

    if (m_liveTranscriptionEnabled == enabled) {
        return;
    }

    m_liveTranscriptionEnabled = enabled;
    emit liveTranscriptionEnabledChanged();

#if defined(Q_OS_ANDROID)
    if (!m_liveTranscriptionEnabled) {
        m_transcriptionSuspendedByPause = false;
        stopTranscriptionSession(true);
        return;
    }

    if (m_isReceivingAudio) {
        startTranscriptionSession();
    }
#endif
}

#if defined(Q_OS_ANDROID)
bool ReflectorClient::hasAuthorizedRecordAudioPermission() const
{
    auto permission = QtAndroidPrivate::checkPermission(kRecordAudioPermission);
    permission.waitForFinished();

    const auto result = permissionResultFromFuture(
        permission,
        "QtAndroidPrivate::checkPermission(RECORD_AUDIO)");
    return result.has_value()
            && result.value() == QtAndroidPrivate::PermissionResult::Authorized;
}

void ReflectorClient::refreshHardwarePttSettings()
{
    const QJniObject context = androidQtContext();
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: Failed to get Android context for hardware PTT refresh";
        return;
    }

    const bool hardwarePttEnabled = QJniObject::callStaticMethod<jboolean>(
        "yo6say/latry/HardwarePttSettingsStore",
        "isPocButtonEnabled",
        "(Landroid/content/Context;)Z",
        context.object());
    const int learnedHardwarePttKeyCode = QJniObject::callStaticMethod<jint>(
        "yo6say/latry/HardwarePttSettingsStore",
        "getLearnedPttKeyCode",
        "(Landroid/content/Context;)I",
        context.object());
    const int normalizedLearnedKeyCode = learnedHardwarePttKeyCode > 0
            ? learnedHardwarePttKeyCode
            : kNoLearnedHardwarePttKeyCode;

    if (m_hardwarePttEnabled == hardwarePttEnabled
            && m_learnedHardwarePttKeyCode == normalizedLearnedKeyCode) {
        return;
    }

    m_hardwarePttEnabled = hardwarePttEnabled;
    m_learnedHardwarePttKeyCode = normalizedLearnedKeyCode;
    emit hardwarePttSettingsChanged();
}

void ReflectorClient::requestRecordAudioPermissionIfNeeded()
{
    if (m_recordAudioPermissionRequestPending) {
        qDebug() << "RECORD_AUDIO permission request already pending";
        return;
    }

    m_recordAudioPermissionRequestPending = true;
    const auto future = QtAndroidPrivate::requestPermission(kRecordAudioPermission);
    auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);

    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, future]() mutable {
        m_recordAudioPermissionRequestPending = false;
        watcher->deleteLater();

        const auto result = permissionResultFromFuture(
            future,
            "QtAndroidPrivate::requestPermission(RECORD_AUDIO)");
        handleRecordAudioPermissionResult(
            result.has_value()
            && result.value() == QtAndroidPrivate::PermissionResult::Authorized);
    });

    watcher->setFuture(future);
}

void ReflectorClient::handleRecordAudioPermissionResult(bool authorized)
{
    const bool retryPtt = m_pttPermissionRestartPending;
    const bool enableTranscription = m_transcriptionPermissionEnablePending;

    m_pttPermissionRestartPending = false;
    m_transcriptionPermissionEnablePending = false;

    if (!authorized) {
        if (enableTranscription) {
            qWarning() << "Live transcription requires RECORD_AUDIO permission";
        }
        if (retryPtt) {
            qWarning() << "RECORD_AUDIO permission denied by user";
        }
        return;
    }

    if (enableTranscription) {
        setLiveTranscriptionEnabled(true);
    }

    if (retryPtt) {
        qDebug() << "RECORD_AUDIO permission granted, proceeding with PTT";
        QTimer::singleShot(100, this, [this]() {
            startTransmission();
        });
    }
}

void ReflectorClient::triggerTxTimeoutWarningHaptic()
{
    const QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: failed to get Android context for TOT warning haptic";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "yo6say/latry/LatryActivity",
        "vibrateTotWarning",
        "(Landroid/content/Context;)V",
        context.object());
}
#endif

void ReflectorClient::downloadTranscriptionModel(const QString &languageTag)
{
#if defined(Q_OS_ANDROID)
    const QString normalizedLanguageTag = languageTag.trimmed();
    if (m_transcriptionModelDownloadInProgress) {
        return;
    }

    const QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: failed to get Android context to download speech model";
        return;
    }

    bool requested = false;
    if (normalizedLanguageTag.isEmpty()) {
        if (m_transcriptionAvailable) {
            return;
        }

        requested = QJniObject::callStaticMethod<jboolean>(
            "yo6say/latry/LatryTranscriptionManager",
            "requestTranscriptionModelDownload",
            "(Landroid/content/Context;)Z",
            context.object());
    } else {
        const QJniObject javaLanguageTag = QJniObject::fromString(normalizedLanguageTag);
        requested = QJniObject::callStaticMethod<jboolean>(
            "yo6say/latry/LatryTranscriptionManager",
            "requestTranscriptionModelDownload",
            "(Landroid/content/Context;Ljava/lang/String;)Z",
            context.object(),
            javaLanguageTag.object<jstring>());
    }
    if (requested) {
        setTranscriptionModelDownloadState(true,
                                           true,
                                           0,
                                           normalizedLanguageTag.isEmpty()
                                                   ? QStringLiteral("Requesting on-device speech model download")
                                                   : QStringLiteral("Requesting on-device speech model download for %1")
                                                         .arg(normalizedLanguageTag));
    }

    refreshTranscriptionSupportState();
#endif
}

void ReflectorClient::openTranscriptionSettings()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: failed to get Android context to open transcription settings";
        return;
    }

    const bool opened = QJniObject::callStaticMethod<jboolean>(
        "yo6say/latry/LatryTranscriptionManager",
        "openTranscriptionSettings",
        "(Landroid/content/Context;)Z",
        context.object());
    if (!opened) {
        qWarning() << "ReflectorClient: failed to open Android voice input settings";
    }
#endif
}

void ReflectorClient::updateTxTimeoutWarningState()
{
    const int remainingSeconds = m_txTimeoutSeconds - m_txSeconds;
    const bool warningActive = m_txTimeoutEnabled
            && m_pttActive
            && !m_pttReleasePending
            && !m_txStopPending
            && remainingSeconds <= kTxTimeoutWarningWindowSeconds
            && remainingSeconds > 0;

    if (m_txTimeoutWarning == warningActive) {
        return;
    }

    m_txTimeoutWarning = warningActive;
    emit txTimeoutWarningChanged();
}

void ReflectorClient::refreshConnectionStatus()
{
    if (m_state != Connected) {
        return;
    }

    m_connectionStatus = (m_talkgroup == 0)
            ? QStringLiteral("Connected in monitor mode")
            : QStringLiteral("Connected to TG %1").arg(m_talkgroup);
    emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
    updateServiceSelectedTalkgroup(m_talkgroup);
    updateServiceConnectionStatus(m_connectionStatus, true);
#endif
}

void ReflectorClient::refreshMonitoredTalkgroupsModel()
{
    emit monitoredTalkgroupsChanged(m_monitoredTalkgroups);
    emit monitoredTalkgroupsModelChanged();
}

void ReflectorClient::setReceivingAudioState(bool receiving)
{
    if (m_isReceivingAudio == receiving) {
#if defined(Q_OS_ANDROID)
        if (!receiving) {
            m_transcriptionSuspendedByPause = false;
        }
#endif
        if (!receiving) {
            stopTranscriptionSession(true);
        }
        return;
    }

    m_isReceivingAudio = receiving;
    if (!receiving) {
        m_audioTimeoutTimer->stop();
    }
    emit isReceivingAudioChanged();

    if (receiving) {
#if defined(Q_OS_ANDROID)
        updateServiceReceiveState(true, m_currentTalker);
        m_transcriptionSuspendedByPause = false;
#endif
        if (m_liveTranscriptionEnabled) {
            startTranscriptionSession();
        }
        return;
    }

#if defined(Q_OS_ANDROID)
    updateServiceReceiveState(false, QString());
    m_transcriptionSuspendedByPause = false;
#endif
    stopTranscriptionSession(true);
}

void ReflectorClient::checkTranscriptionAvailability(bool androidServiceLaunch)
{
    setTranscriptionAvailable(false);
    setTranscriptionLanguageModels(QStringList{}, QStringList{}, QStringList{}, QString());
    setTranscriptionModelDownloadState(false, false, -1, QString());
    if (m_transcriptionSupportRefreshTimer) {
        m_transcriptionSupportRefreshTimer->stop();
    }

#if defined(Q_OS_ANDROID)
    if (androidServiceLaunch) {
        return;
    }

    refreshTranscriptionSupportState();
#else
    Q_UNUSED(androidServiceLaunch)
#endif
}

void ReflectorClient::refreshTranscriptionSupportState()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: failed to get Android context for transcription support";
        setTranscriptionAvailable(false);
        setTranscriptionLanguageModels(QStringList{}, QStringList{}, QStringList{}, QString());
        setTranscriptionModelDownloadState(false, false, -1, QString());
        return;
    }

    const QJniObject supportJson = QJniObject::callStaticObjectMethod(
        "yo6say/latry/LatryTranscriptionManager",
        "getTranscriptionSupportJson",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object());
    const QString jsonText = supportJson.toString();
    if (jsonText.isEmpty()) {
        qWarning() << "ReflectorClient: empty transcription support state from Android";
        setTranscriptionAvailable(false);
        setTranscriptionLanguageModels(QStringList{}, QStringList{}, QStringList{}, QString());
        setTranscriptionModelDownloadState(false, false, -1, QString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument supportDocument =
            QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !supportDocument.isObject()) {
        qWarning() << "ReflectorClient: failed to parse transcription support state"
                   << parseError.errorString() << jsonText;
        setTranscriptionAvailable(false);
        setTranscriptionLanguageModels(QStringList{}, QStringList{}, QStringList{}, QString());
        setTranscriptionModelDownloadState(false, false, -1, QString());
        return;
    }

    const QJsonObject supportObject = supportDocument.object();
    const QStringList installedLanguages =
            jsonStringList(supportObject.value(QStringLiteral("installedLanguages")));
    const QStringList pendingLanguages =
            jsonStringList(supportObject.value(QStringLiteral("pendingLanguages")));
    const QStringList supportedLanguages =
            jsonStringList(supportObject.value(QStringLiteral("supportedLanguages")));
    const QString downloadTargetLanguage =
            supportObject.value(QStringLiteral("downloadTargetLanguage")).toString().trimmed();

    setTranscriptionAvailable(supportObject.value(QStringLiteral("available")).toBool(false));
    setTranscriptionLanguageModels(installedLanguages,
                                   pendingLanguages,
                                   supportedLanguages,
                                   downloadTargetLanguage);
    setTranscriptionModelDownloadState(
        supportObject.value(QStringLiteral("canDownload")).toBool(false),
        supportObject.value(QStringLiteral("downloadInProgress")).toBool(false),
        supportObject.value(QStringLiteral("downloadProgress")).toInt(-1),
        supportObject.value(QStringLiteral("statusMessage")).toString());
#endif
}

void ReflectorClient::startTranscriptionSession()
{
#if defined(Q_OS_ANDROID)
    if (!m_liveTranscriptionEnabled || !m_transcriptionAvailable
            || !m_isReceivingAudio || !m_audioEngine || m_transcriptionSessionActive) {
        return;
    }

    m_transcriptionPendingText.clear();
    m_lastFinalTranscriptionSegment.clear();
    updateTranscriptionDisplay();

    const QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");
    if (!context.isValid()) {
        qWarning() << "ReflectorClient: failed to get Android context to start live transcription";
        return;
    }

    const jint writeFd = QJniObject::callStaticMethod<jint>(
        "yo6say/latry/LatryTranscriptionManager",
        "createPipeAndStart",
        "(Landroid/content/Context;)I",
        context.object());
    if (writeFd < 0) {
        qWarning() << "ReflectorClient: live transcription session failed to start";
        return;
    }

    const bool forwarded = QMetaObject::invokeMethod(
        m_audioEngine,
        "setTranscriptionPipeFd",
        Qt::BlockingQueuedConnection,
        Q_ARG(int, static_cast<int>(writeFd)));
    if (!forwarded) {
        ::close(writeFd);
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryTranscriptionManager",
                                           "stopTranscription",
                                           "()V");
        qWarning() << "ReflectorClient: failed to forward transcription pipe to AudioEngine";
        return;
    }

    m_transcriptionSessionActive = true;
#endif
}

void ReflectorClient::stopTranscriptionSession(bool clearText)
{
#if defined(Q_OS_ANDROID)
    const bool hadActiveSession = m_transcriptionSessionActive;
    m_transcriptionSessionActive = false;

    if (m_audioEngine && m_audioThread && m_audioThread->isRunning()) {
        QMetaObject::invokeMethod(
            m_audioEngine,
            "setTranscriptionPipeFd",
            Qt::BlockingQueuedConnection,
            Q_ARG(int, -1));
    }

    if (hadActiveSession) {
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryTranscriptionManager",
                                           "stopTranscription",
                                           "()V");
    }
#endif

    if (clearText) {
        clearTranscriptionState();
    }
}

void ReflectorClient::setTranscriptionAvailable(bool available)
{
    if (m_transcriptionAvailable == available) {
        return;
    }

    m_transcriptionAvailable = available;
    emit transcriptionAvailabilityChanged();
}

void ReflectorClient::setTranscriptionLanguageModels(const QStringList &installedLanguages,
                                                     const QStringList &pendingLanguages,
                                                     const QStringList &supportedLanguages,
                                                     const QString &downloadTargetLanguage)
{
    QVariantList installedModels;
    installedModels.reserve(installedLanguages.size());
    for (const QString &languageTag : installedLanguages) {
        installedModels.append(buildTranscriptionLanguageEntry(languageTag,
                                                              true,
                                                              false,
                                                              false));
    }

    QVariantList downloadableModels;
    QStringList orderedDownloadableTags = pendingLanguages;
    for (const QString &languageTag : supportedLanguages) {
        if (!orderedDownloadableTags.contains(languageTag)) {
            orderedDownloadableTags.append(languageTag);
        }
    }

    downloadableModels.reserve(orderedDownloadableTags.size());
    for (const QString &languageTag : orderedDownloadableTags) {
        downloadableModels.append(buildTranscriptionLanguageEntry(
                languageTag,
                false,
                pendingLanguages.contains(languageTag),
                !downloadTargetLanguage.isEmpty() && downloadTargetLanguage == languageTag));
    }

    if (m_transcriptionInstalledLanguages == installedModels
            && m_transcriptionDownloadableLanguages == downloadableModels) {
        return;
    }

    m_transcriptionInstalledLanguages = installedModels;
    m_transcriptionDownloadableLanguages = downloadableModels;
    emit transcriptionLanguageModelsChanged();
}

void ReflectorClient::setTranscriptionModelDownloadState(bool available,
                                                         bool inProgress,
                                                         int progress,
                                                         const QString &status)
{
    bool changed = false;

    if (m_transcriptionModelDownloadAvailable != available) {
        m_transcriptionModelDownloadAvailable = available;
        changed = true;
    }
    if (m_transcriptionModelDownloadInProgress != inProgress) {
        m_transcriptionModelDownloadInProgress = inProgress;
        changed = true;
    }
    if (m_transcriptionModelDownloadProgress != progress) {
        m_transcriptionModelDownloadProgress = progress;
        changed = true;
    }
    if (m_transcriptionModelDownloadStatus != status) {
        m_transcriptionModelDownloadStatus = status;
        changed = true;
    }

    if (m_transcriptionSupportRefreshTimer) {
        if (m_transcriptionModelDownloadInProgress) {
            m_transcriptionSupportRefreshTimer->start();
        } else {
            m_transcriptionSupportRefreshTimer->stop();
        }
    }

    if (changed) {
        emit transcriptionModelDownloadStateChanged();
    }
}

QString ReflectorClient::normalizeTranscriptionSnippet(const QString &text)
{
    return text.simplified();
}

void ReflectorClient::clearTranscriptionState()
{
    m_transcriptionCommittedText.clear();
    m_transcriptionPendingText.clear();
    m_lastFinalTranscriptionSegment.clear();
    updateTranscriptionDisplay();
}

void ReflectorClient::updateTranscriptionDisplay()
{
    QString nextText = trimTranscriptionTail(m_transcriptionCommittedText);
    const QString pending = trimTranscriptionTail(m_transcriptionPendingText);
    if (!pending.isEmpty()) {
        nextText = nextText.isEmpty() ? pending : nextText + QLatin1Char('\n') + pending;
    }

    nextText = trimTranscriptionTail(nextText);
    if (m_transcriptionText == nextText) {
        return;
    }

    m_transcriptionText = nextText;
    emit transcriptionTextChanged();
}

void ReflectorClient::handlePartialTranscription(const QString &text)
{
    if (!m_transcriptionSessionActive) {
        return;
    }

    m_transcriptionPendingText = normalizeTranscriptionSnippet(text);
    updateTranscriptionDisplay();
}

void ReflectorClient::handleFinalTranscription(const QString &text)
{
    if (!m_transcriptionSessionActive) {
        return;
    }

    const QString normalized = normalizeTranscriptionSnippet(text);
    m_transcriptionPendingText.clear();
    if (normalized.isEmpty()) {
        updateTranscriptionDisplay();
        return;
    }

    if (normalized != m_lastFinalTranscriptionSegment) {
        m_lastFinalTranscriptionSegment = normalized;
        m_transcriptionCommittedText = m_transcriptionCommittedText.isEmpty()
                ? normalized
                : trimTranscriptionTail(m_transcriptionCommittedText + QLatin1Char('\n') + normalized);
    }

    updateTranscriptionDisplay();
}

void ReflectorClient::handleTranscriptionError(int errorCode, const QString &message)
{
    qWarning() << "ReflectorClient: live transcription stopped after error"
               << errorCode << message;

    if (errorCode == kAndroidSpeechErrorLanguageNotSupported
            || errorCode == kAndroidSpeechErrorLanguageUnavailable) {
        const bool wasEnabled = m_liveTranscriptionEnabled;
        m_liveTranscriptionEnabled = false;
        if (wasEnabled) {
            emit liveTranscriptionEnabledChanged();
        }
        setTranscriptionAvailable(false);
        refreshTranscriptionSupportState();
    }

    m_transcriptionSessionActive = false;
    m_transcriptionPendingText.clear();
    stopTranscriptionSession(false);
    updateTranscriptionDisplay();
}

void ReflectorClient::setAudioRouteState(const QString &currentRoute, const QStringList &availableRouteIds)
{
    const QStringList orderedRouteIds = normalizedAudioRouteIds(availableRouteIds);
    const QVariantList routeModel = buildAudioRouteModel(orderedRouteIds);

    QString normalizedCurrentRoute = normalizeAudioRouteId(currentRoute);
    if (!orderedRouteIds.contains(normalizedCurrentRoute)) {
        normalizedCurrentRoute = orderedRouteIds.isEmpty() ? kAudioRouteSpeaker : orderedRouteIds.constFirst();
    }

    const bool availableRoutesChanged = (m_availableAudioRoutes != routeModel);
    const bool currentRouteChanged = (m_currentAudioRoute != normalizedCurrentRoute);

    if (availableRoutesChanged) {
        m_availableAudioRoutes = routeModel;
        emit availableAudioRoutesChanged();
    }

    if (currentRouteChanged) {
        m_currentAudioRoute = normalizedCurrentRoute;
        emit currentAudioRouteChanged();

        if (m_audioEngine) {
            QMetaObject::invokeMethod(m_audioEngine, "handleAudioRouteChanged", Qt::QueuedConnection);
        }
    }
}

void ReflectorClient::applyAudioLevelsToEngine()
{
    if (!m_audioEngine) {
        return;
    }

    QMetaObject::invokeMethod(m_audioEngine, "setRxAudioLevelDb",
                              Qt::QueuedConnection,
                              Q_ARG(float, static_cast<float>(m_rxAudioLevelDb)));
    QMetaObject::invokeMethod(m_audioEngine, "setTxAudioLevelDb",
                              Qt::QueuedConnection,
                              Q_ARG(float, static_cast<float>(m_txAudioLevelDb)));
}

void ReflectorClient::setRxMeterState(qreal level, qreal peakLevel)
{
    const qreal normalizedLevel = normalizeMeterLevel(level);
    const qreal normalizedPeakLevel = normalizeMeterLevel(peakLevel);

    if (m_rxMeterLevel != normalizedLevel) {
        m_rxMeterLevel = normalizedLevel;
        emit rxMeterLevelChanged();
    }
    if (m_rxMeterPeakLevel != normalizedPeakLevel) {
        m_rxMeterPeakLevel = normalizedPeakLevel;
        emit rxMeterPeakLevelChanged();
    }
}

void ReflectorClient::setTxMeterState(qreal level, qreal peakLevel)
{
    const qreal normalizedLevel = normalizeMeterLevel(level);
    const qreal normalizedPeakLevel = normalizeMeterLevel(peakLevel);

    if (m_txMeterLevel != normalizedLevel) {
        m_txMeterLevel = normalizedLevel;
        emit txMeterLevelChanged();
    }
    if (m_txMeterPeakLevel != normalizedPeakLevel) {
        m_txMeterPeakLevel = normalizedPeakLevel;
        emit txMeterPeakLevelChanged();
    }
}

void ReflectorClient::resetAudioMeters()
{
    setRxMeterState(0.0, 0.0);
    setTxMeterState(0.0, 0.0);
}

void ReflectorClient::resetTalkgroupSelectionTimer()
{
    if (m_talkgroup == 0) {
        stopTalkgroupSelectionTimer();
        return;
    }

    m_tgSelectTimeoutCounter = m_tgSelectTimeoutSeconds;
    if (m_state == Connected && !m_talkgroupSelectionTimer->isActive()) {
        m_talkgroupSelectionTimer->start();
    }
}

void ReflectorClient::stopTalkgroupSelectionTimer()
{
    m_tgSelectTimeoutCounter = 0;
    if (m_talkgroupSelectionTimer->isActive()) {
        m_talkgroupSelectionTimer->stop();
    }
}

void ReflectorClient::clearMonitoredTalkgroups()
{
    if (!m_monitoredTalkgroups.isEmpty()) {
        m_monitoredTalkgroups.clear();
        refreshMonitoredTalkgroupsModel();
    }
}

quint8 ReflectorClient::monitoredTalkgroupPriority(quint32 talkgroup) const
{
    for (const MonitoredTalkgroupEntry &entry : m_configuredMonitoredTalkgroups) {
        if (entry.talkgroup == talkgroup) {
            return entry.priority;
        }
    }
    return 0;
}

bool ReflectorClient::isTalkgroupMonitored(quint32 talkgroup) const
{
    return m_monitoredTalkgroups.contains(talkgroup);
}

bool ReflectorClient::shouldHandleTalkerStart(quint32 talkgroup)
{
    if (talkgroup == 0) {
        qWarning() << "Ignoring TALKER_START on TG 0 because TG 0 is only the local monitor parking state";
        return false;
    }

    if (talkgroup == m_talkgroup) {
        if (m_talkgroup > 0) {
            resetTalkgroupSelectionTimer();
        }
        return true;
    }

    if (!isTalkgroupMonitored(talkgroup)) {
        return false;
    }

    if (m_talkgroup == 0) {
        selectTalkgroupInternal(talkgroup, TalkgroupSelectionOrigin::RemoteActivation);
        return true;
    }

    if (!m_usePriorityMode) {
        return false;
    }

    const quint8 incomingPriority = monitoredTalkgroupPriority(talkgroup);
    const quint8 selectedPriority = monitoredTalkgroupPriority(m_talkgroup);
    if (incomingPriority > selectedPriority) {
        selectTalkgroupInternal(talkgroup, TalkgroupSelectionOrigin::RemotePriorityActivation);
        return true;
    }

    return false;
}

bool ReflectorClient::selectTalkgroupInternal(quint32 talkgroup, TalkgroupSelectionOrigin origin)
{
    const bool talkgroupChanged = (m_talkgroup != talkgroup);
    const bool manualSelection = (origin == TalkgroupSelectionOrigin::Manual
                                  || origin == TalkgroupSelectionOrigin::TxDefaultActivation);

    if (talkgroupChanged) {
        m_talkgroup = talkgroup;
        emit selectedTalkgroupChanged();
    }

    if (talkgroup == 0) {
        m_usePriorityMode = true;
        stopTalkgroupSelectionTimer();
    } else {
        if (manualSelection) {
            m_usePriorityMode = false;
        }
        resetTalkgroupSelectionTimer();
    }

    if (m_state != Connected) {
        qInfo() << "Stored talkgroup for next connection:" << m_talkgroup;
        return talkgroupChanged;
    }

    if (talkgroupChanged) {
        qInfo() << "Selecting talkgroup:" << m_talkgroup;
        sendSelectTG(m_talkgroup);
    }

    refreshConnectionStatus();
    return talkgroupChanged;
}

ReflectorClient::ParsedMonitoredTalkgroups ReflectorClient::parseMonitoredTalkgroupsSpec(
        const QString &monitoredTalkgroups, quint32 primaryTalkgroup)
{
    ParsedMonitoredTalkgroups parsed;
    QSet<quint32> seenConfigured;
    QStringList normalizedEntries;

    const QStringList rawEntries = monitoredTalkgroups.split(',', Qt::SkipEmptyParts);
    for (const QString &rawEntry : rawEntries) {
        const QString trimmedEntry = rawEntry.trimmed();
        if (trimmedEntry.isEmpty()) {
            continue;
        }

        int suffixStart = trimmedEntry.size();
        int priority = 0;
        while (suffixStart > 0 && trimmedEntry.at(suffixStart - 1) == QLatin1Char('+')) {
            ++priority;
            --suffixStart;
        }

        const QString talkgroupText = trimmedEntry.left(suffixStart).trimmed();
        bool ok = false;
        const quint32 talkgroup = talkgroupText.toUInt(&ok);
        if (!ok || talkgroup == 0 || seenConfigured.contains(talkgroup)) {
            continue;
        }

        MonitoredTalkgroupEntry entry;
        entry.talkgroup = talkgroup;
        entry.priority = static_cast<quint8>(qMin(priority, 255));
        parsed.configured.append(entry);
        seenConfigured.insert(talkgroup);
        normalizedEntries.append(QString::number(talkgroup) + QString(entry.priority, QLatin1Char('+')));
    }

    parsed.normalizedSpec = normalizedEntries.join(QLatin1Char(','));

    QSet<quint32> seenActive;
    if (primaryTalkgroup > 0) {
        parsed.activeTalkgroups.append(primaryTalkgroup);
        seenActive.insert(primaryTalkgroup);
    }

    for (const MonitoredTalkgroupEntry &entry : parsed.configured) {
        if (!seenActive.contains(entry.talkgroup)) {
            parsed.activeTalkgroups.append(entry.talkgroup);
            seenActive.insert(entry.talkgroup);
        }
    }

    return parsed;
}

void ReflectorClient::applyMonitoredTalkgroups(const QString &monitoredTalkgroups)
{
    const ParsedMonitoredTalkgroups parsed = parseMonitoredTalkgroupsSpec(monitoredTalkgroups, m_defaultTalkgroup);
    m_monitoredTalkgroupsSpec = parsed.normalizedSpec;
    m_configuredMonitoredTalkgroups = parsed.configured;

    if (m_monitoredTalkgroups != parsed.activeTalkgroups) {
        m_monitoredTalkgroups = parsed.activeTalkgroups;
        refreshMonitoredTalkgroupsModel();
    }
}

void ReflectorClient::onTalkgroupSelectionTimer()
{
    if (m_state != Connected || m_talkgroup == 0 || m_tgSelectTimeoutCounter <= 0) {
        if (m_tgSelectTimeoutCounter <= 0) {
            stopTalkgroupSelectionTimer();
        }
        return;
    }

    if (m_pttActive || m_isReceivingAudio) {
        return;
    }

    --m_tgSelectTimeoutCounter;
    if (m_tgSelectTimeoutCounter == 0) {
        selectTalkgroupInternal(0, TalkgroupSelectionOrigin::Timeout);
    }
}

// --- Audio Engine Management ---

void ReflectorClient::setupAudio()
{
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setupAudio", Qt::QueuedConnection);
    }
}

void ReflectorClient::initializeAudioEngine()
{
    // Create audio thread with time-critical priority
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngineThread");
    m_audioThread->start(QThread::TimeCriticalPriority);

    // Create audio engine and move to audio thread
    m_audioEngine = new AudioEngine();
    m_audioEngine->moveToThread(m_audioThread);

    // Connect signals from AudioEngine to ReflectorClient
    connect(m_audioEngine, &AudioEngine::audioReadyChanged, this, [this](bool ready) {
        m_audioReady = ready;
        emit audioReadyChanged();
#if defined(Q_OS_ANDROID)
        if (ready) {
            resumeAndroidPttAfterReconnectIfReady();
        }
#endif
    });

    connect(m_audioEngine, &AudioEngine::audioDataEncoded, this, &ReflectorClient::onAudioDataEncoded);
    connect(m_audioEngine, &AudioEngine::txDrainComplete, this, &ReflectorClient::onTxDrainComplete);
    connect(m_audioEngine, &AudioEngine::audioSetupFinished, this, &ReflectorClient::onAudioSetupFinished);
    connect(m_audioEngine, &AudioEngine::rxMeterLevelsChanged, this,
            [this](float level, float peakLevel) {
                setRxMeterState(level, peakLevel);
            });
    connect(m_audioEngine, &AudioEngine::txMeterLevelsChanged, this,
            [this](float level, float peakLevel) {
                setTxMeterState(level, peakLevel);
            });

    // Connect Android audio focus signals
    connect(this, &ReflectorClient::audioFocusLost, m_audioEngine, &AudioEngine::onAudioFocusLost);
    connect(this, &ReflectorClient::audioFocusPaused, m_audioEngine, &AudioEngine::onAudioFocusPaused);
    connect(this, &ReflectorClient::audioFocusGained, m_audioEngine, &AudioEngine::onAudioFocusGained);
    connect(this, &ReflectorClient::activityPaused, m_audioEngine, &AudioEngine::onActivityPaused);
    connect(this, &ReflectorClient::activityResumed, m_audioEngine, &AudioEngine::onActivityResumed);

    applyAudioLevelsToEngine();
}

#if defined(Q_OS_ANDROID)
void ReflectorClient::handleAndroidAudioRoutesChanged(const QString &currentRoute,
                                                      const QStringList &availableRoutes)
{
    setAudioRouteState(currentRoute, availableRoutes);
}
#endif

void ReflectorClient::onAudioSetupFinished()
{
    if (!m_audioReady) {
        m_audioReady = true;
        emit audioReadyChanged();
    }
#if defined(Q_OS_ANDROID)
    resumeAndroidPttAfterReconnectIfReady();
#endif
}