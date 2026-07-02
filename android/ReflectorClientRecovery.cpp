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

#include <QDebug>
#include <QMetaObject>
#include <QRandomGenerator>
#include <iterator>

namespace {
constexpr int kReconnectBackoffScheduleMs[] = {0, 1000, 2000, 5000, 10000, 15000, 30000};
constexpr int kHeartbeatMissThreshold = 3;
constexpr int kMinimumLivenessTimeoutMs = 15000;
constexpr int kMaximumLivenessTimeoutMs = 120000;
constexpr int kMaxRecentHeartbeatIntervals = 5;
constexpr int kAndroidNetworkReasonInitial = 1;
}

bool ReflectorClient::resolveReconnectContext(ReconnectContext &context)
{
    context.host = m_host;
    context.port = m_port;
    context.authKey = m_authKey;
    context.callsign = m_callsign;
    context.talkgroup = m_talkgroup;
    context.monitoredTalkgroups = m_monitoredTalkgroupsSpec;
    context.tgSelectTimeoutSeconds = m_tgSelectTimeoutSeconds;

#if defined(Q_OS_ANDROID)
    if (!context.isValid()) {
        loadSavedAndroidReconnectProfile(context.host, context.port, context.authKey,
                                         context.callsign, context.talkgroup,
                                         context.monitoredTalkgroups,
                                         context.tgSelectTimeoutSeconds);
    }
#endif

    return context.isValid();
}

void ReflectorClient::transitionToDisconnectedState(const QString &status, bool preserveReconnectContext)
{
    const bool transmitWasActive = m_pttActive || m_pttReleasePending || m_txStopPending;
#if defined(Q_OS_ANDROID)
    if (!preserveReconnectContext) {
        m_resumeAndroidPttAfterReconnect = false;
    } else if (transmitWasActive) {
        m_resumeAndroidPttAfterReconnect = true;
    }
#endif
    if (transmitWasActive) {
        stopTransmissionCaptureForDisconnect();
    }

    cancelPendingPttRelease();
    m_pttReleasePending = false;
    m_txStopPending = false;

    if (m_pttActive) {
        m_pttActive = false;
        emit pttActiveChanged();
    }

    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    if (m_txTimer) {
        const bool txStateChanged = m_txTimer->isActive() || m_txSeconds != 0;
        m_txTimer->stop();
        m_txTimeoutWarningFeedbackSent = false;
        if (txStateChanged) {
            m_txSeconds = 0;
            emit txTimeStringChanged();
        }
    }
    if (m_pttHangTimer) {
        m_pttHangTimer->stop();
    }
    if (m_connectTimer) {
        m_connectTimer->stop();
    }
    if (m_audioTimeoutTimer) {
        m_audioTimeoutTimer->stop();
    }

    m_lastAudioSeq = 0;
    m_tcpBuffer.clear();
    m_udpSocket->close();

    if (!m_currentTalker.isEmpty()) {
        m_currentTalker.clear();
        emit currentTalkerChanged();
    }
    setReceivingAudioState(false);
    resetAudioMeters();
    stopTalkgroupSelectionTimer();
    m_usePriorityMode = true;

    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }

    if (m_nameReply) {
        m_nameReply->abort();
        QMetaObject::invokeMethod(m_nameReply, "deleteLater", Qt::QueuedConnection);
        m_nameReply = nullptr;
    }

    if (!preserveReconnectContext) {
        clearMonitoredTalkgroups();
        m_authKey.clear();
        clearReconnectSchedule();
        m_reconnectBackoffStep = 0;
        m_waitingForValidatedNetwork = false;
#if defined(Q_OS_ANDROID)
        m_resumeAndroidPttAfterReconnect = false;
#endif
    }

    m_clientId = 0;
    m_udpSequence = 0;
    resetProtocolLivenessWatchdog();

    const State previousState = m_state;
    const QString previousStatus = m_connectionStatus;
    m_state = Disconnected;
    m_connectionStatus = status;

    if (m_audioReady) {
        m_audioReady = false;
        emit audioReadyChanged();
    }

    if (previousState != Disconnected || previousStatus != m_connectionStatus) {
        emit connectionStatusChanged();
    }

#if defined(Q_OS_ANDROID)
    updateServiceConnectionStatus(m_connectionStatus, false);
    updateServiceCurrentTalker(QString());
    updateServiceReceiveState(false, QString());
    updateServiceTransmitState(false);
#endif
    updateTxTimeoutWarningState();
}

void ReflectorClient::resetReconnectBackoff()
{
    m_reconnectBackoffStep = 0;
}

void ReflectorClient::clearReconnectSchedule()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
}

bool ReflectorClient::hasValidatedNetworkForReconnect() const
{
    if (!m_androidNetworkStateKnown) {
        return true;
    }

    return m_hasDefaultNetwork && m_validatedDefaultNetwork && !m_androidNetworkCaptivePortal;
}

void ReflectorClient::setWaitingForValidatedNetwork(bool waiting)
{
    if (m_waitingForValidatedNetwork == waiting && (!waiting || m_connectionStatus == QStringLiteral("Waiting for validated network..."))) {
        return;
    }

    m_waitingForValidatedNetwork = waiting;
    if (!waiting) {
        return;
    }

    clearReconnectSchedule();
    const QString waitingStatus = QStringLiteral("Waiting for validated network...");
    if (m_state != Disconnected || m_connectionStatus != waitingStatus) {
        m_state = Disconnected;
        m_connectionStatus = waitingStatus;
        emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
        updateServiceConnectionStatus(m_connectionStatus, false);
#endif
    }
}

QString ReflectorClient::reconnectStatusText() const
{
    return m_host.isEmpty()
            ? QStringLiteral("Reconnecting...")
            : QStringLiteral("Reconnecting to %1...").arg(m_host);
}

void ReflectorClient::scheduleReconnectAttempt(const QString &reason, bool immediate)
{
    ReconnectContext reconnectContext;
    if (!resolveReconnectContext(reconnectContext)) {
        qWarning() << "Reconnect requested without a valid connection profile:" << reason;
        return;
    }

    if (!hasValidatedNetworkForReconnect()) {
        qInfo() << "Delaying reconnect until a validated default network is available:" << reason;
        setWaitingForValidatedNetwork(true);
        return;
    }

    setWaitingForValidatedNetwork(false);

    int delayMs = 0;
    if (!immediate) {
        const int stepIndex = qMin(m_reconnectBackoffStep,
                                   int(std::size(kReconnectBackoffScheduleMs) - 1));
        delayMs = kReconnectBackoffScheduleMs[stepIndex];
        const int jitterRange = qMax(100, delayMs / 4);
        if (delayMs > 0) {
            delayMs += QRandomGenerator::global()->bounded(jitterRange);
        }
        if (m_reconnectBackoffStep < int(std::size(kReconnectBackoffScheduleMs) - 1)) {
            ++m_reconnectBackoffStep;
        }
    } else {
        resetReconnectBackoff();
    }

    if (m_reconnectTimer->isActive()) {
        if (!immediate && m_reconnectTimer->remainingTime() <= delayMs) {
            return;
        }
        m_reconnectTimer->stop();
    }

    const QString pendingStatus = delayMs == 0
            ? reconnectStatusText()
            : QStringLiteral("Reconnecting in %1 s...").arg(qMax(1, (delayMs + 999) / 1000));
    if (m_state != Disconnected || m_connectionStatus != pendingStatus) {
        m_state = Disconnected;
        m_connectionStatus = pendingStatus;
        emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
        updateServiceConnectionStatus(m_connectionStatus, false);
#endif
    }

    qInfo() << "Scheduling reconnect in" << delayMs << "ms:" << reason;
    m_reconnectTimer->start(delayMs);
}

void ReflectorClient::onReconnectBackoffTimeout()
{
    ReconnectContext reconnectContext;
    if (!resolveReconnectContext(reconnectContext)) {
        qWarning() << "Reconnect timer fired without a valid reconnect profile";
        return;
    }

    if (!hasValidatedNetworkForReconnect()) {
        setWaitingForValidatedNetwork(true);
        return;
    }

    if (m_state != Disconnected) {
        qDebug() << "Ignoring reconnect timer because client state is" << m_state;
        return;
    }

    qInfo() << "Attempting reconnect using saved profile"
            << reconnectContext.host << reconnectContext.port;
    connectToServer(reconnectContext.host,
                    reconnectContext.port,
                    QString::fromUtf8(reconnectContext.authKey),
                    reconnectContext.callsign,
                    reconnectContext.talkgroup,
                    reconnectContext.monitoredTalkgroups,
                    reconnectContext.tgSelectTimeoutSeconds);
}

void ReflectorClient::resetProtocolLivenessWatchdog()
{
    if (m_protocolLivenessTimer) {
        m_protocolLivenessTimer->stop();
    }
    m_protocolHeartbeatClock.invalidate();
    m_lastInboundHeartbeatMs = -1;
    m_recentInboundHeartbeatIntervals.clear();
}

void ReflectorClient::noteInboundProtocolHeartbeat()
{
    if (m_state != Connected) {
        return;
    }

    if (!m_protocolHeartbeatClock.isValid()) {
        m_protocolHeartbeatClock.start();
        m_lastInboundHeartbeatMs = 0;
        return;
    }

    const qint64 nowMs = m_protocolHeartbeatClock.elapsed();
    if (m_lastInboundHeartbeatMs >= 0) {
        const qint64 intervalMs = nowMs - m_lastInboundHeartbeatMs;
        if (intervalMs > 0) {
            m_recentInboundHeartbeatIntervals.append(intervalMs);
            while (m_recentInboundHeartbeatIntervals.size() > kMaxRecentHeartbeatIntervals) {
                m_recentInboundHeartbeatIntervals.removeFirst();
            }
        }
    }
    m_lastInboundHeartbeatMs = nowMs;

    const int timeoutMs = protocolLivenessTimeoutMs();
    if (timeoutMs > 0) {
        m_protocolLivenessTimer->start(timeoutMs);
    }
}

int ReflectorClient::protocolLivenessTimeoutMs() const
{
    if (m_recentInboundHeartbeatIntervals.isEmpty()) {
        return 0;
    }

    qint64 longestObservedInterval = 0;
    for (qint64 intervalMs : m_recentInboundHeartbeatIntervals) {
        longestObservedInterval = qMax(longestObservedInterval, intervalMs);
    }

    if (longestObservedInterval <= 0) {
        return 0;
    }

    const qint64 timeoutMs = qMax<qint64>(
            static_cast<qint64>(kMinimumLivenessTimeoutMs),
            qMin<qint64>(longestObservedInterval * kHeartbeatMissThreshold,
                         static_cast<qint64>(kMaximumLivenessTimeoutMs)));
    return static_cast<int>(timeoutMs);
}

void ReflectorClient::onProtocolLivenessTimeout()
{
    if (m_state != Connected && m_state != Authenticating && m_state != Connecting) {
        return;
    }

    qWarning() << "Inbound protocol heartbeat watchdog expired; forcing reconnect";

    const bool socketActive = m_tcpSocket
            && m_tcpSocket->state() != QAbstractSocket::UnconnectedState;
    if (socketActive) {
        m_ignoreNextSocketDisconnect = true;
        m_ignoreNextSocketError = true;
        m_tcpSocket->abort();
    }

    transitionToDisconnectedState(QStringLiteral("Connection lost, reconnecting..."), true);

    if (!socketActive) {
        m_ignoreNextSocketDisconnect = false;
        m_ignoreNextSocketError = false;
    }

    scheduleReconnectAttempt(QStringLiteral("protocol heartbeat watchdog"), true);
}

void ReflectorClient::handleAndroidNetworkStateChanged(int generation,
                                                       int reason,
                                                       bool hasDefaultNetwork,
                                                       bool validated,
                                                       int transport,
                                                       bool metered,
                                                       bool captivePortal,
                                                       bool routeChanged)
{
    const bool hadPreviousSnapshot = m_androidNetworkStateKnown;
    const bool wasWaitingForNetwork = m_waitingForValidatedNetwork;

    m_androidNetworkStateKnown = true;
    m_hasDefaultNetwork = hasDefaultNetwork;
    m_validatedDefaultNetwork = validated;
    m_androidNetworkTransport = static_cast<AndroidNetworkTransport>(transport);
    m_androidNetworkMetered = metered;
    m_androidNetworkCaptivePortal = captivePortal;
    m_lastAndroidNetworkGeneration = generation;

    const bool validatedNetworkAvailable = hasDefaultNetwork && validated && !captivePortal;
    if (!validatedNetworkAvailable) {
        const bool socketActive = m_tcpSocket
                && m_tcpSocket->state() != QAbstractSocket::UnconnectedState;
        if (socketActive || m_state == Connecting || m_state == Authenticating || m_state == Connected) {
            qInfo() << "Validated default network is unavailable; waiting for recovery";
            if (socketActive) {
                m_ignoreNextSocketDisconnect = true;
                m_ignoreNextSocketError = true;
                m_tcpSocket->abort();
            }
            transitionToDisconnectedState(QStringLiteral("Waiting for validated network..."), true);
            if (!socketActive) {
                m_ignoreNextSocketDisconnect = false;
                m_ignoreNextSocketError = false;
            }
        } else {
            setWaitingForValidatedNetwork(true);
        }
        return;
    }

    setWaitingForValidatedNetwork(false);

    const bool meaningfulRouteChange = hadPreviousSnapshot
            && reason != kAndroidNetworkReasonInitial
            && routeChanged;
    if (meaningfulRouteChange
            && (m_state == Connected || m_state == Authenticating || m_state == Connecting)) {
        qInfo() << "Validated default route changed; restarting reflector session";
        const bool socketActive = m_tcpSocket
                && m_tcpSocket->state() != QAbstractSocket::UnconnectedState;
        if (socketActive) {
            m_ignoreNextSocketDisconnect = true;
            m_ignoreNextSocketError = true;
            m_tcpSocket->abort();
        }
        transitionToDisconnectedState(QStringLiteral("Network changed, reconnecting..."), true);
        if (!socketActive) {
            m_ignoreNextSocketDisconnect = false;
            m_ignoreNextSocketError = false;
        }
        scheduleReconnectAttempt(QStringLiteral("validated default network change"), true);
        return;
    }

    if (wasWaitingForNetwork || m_state == Disconnected) {
        scheduleReconnectAttempt(wasWaitingForNetwork
                                         ? QStringLiteral("validated network restored")
                                         : QStringLiteral("validated default network available"),
                                 true);
    }
}
