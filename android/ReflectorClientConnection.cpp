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
#include <QHostAddress>
#include <QDebug>
#include <QMetaObject>

void ReflectorClient::connectToServer(const QString &host, int port, const QString &authKey, const QString &callsign,
                                      quint32 talkgroup, const QString &monitoredTalkgroups,
                                      int tgSelectTimeoutSeconds)
{
    if (m_state != Disconnected) return;

    clearReconnectSchedule();
    resetReconnectBackoff();
    setWaitingForValidatedNetwork(false);
    resetProtocolLivenessWatchdog();
    m_ignoreNextSocketDisconnect = false;
    m_ignoreNextSocketError = false;

    // Capture the selected server settings before Android persists or advertises them.
    m_host = host.trimmed();
    m_port = port;
    m_authKey = authKey.trimmed().toUtf8();
    m_callsign = callsign.trimmed();
    if (m_talkgroup != talkgroup) {
        m_talkgroup = talkgroup;
        emit selectedTalkgroupChanged();
    } else {
        m_talkgroup = talkgroup;
    }
    m_tgSelectTimeoutSeconds = normalizeTgSelectTimeoutSeconds(tgSelectTimeoutSeconds);
    if (m_defaultTalkgroup == 0) {
        m_defaultTalkgroup = talkgroup;
    }
    applyMonitoredTalkgroups(monitoredTalkgroups);
    stopTalkgroupSelectionTimer();
    m_usePriorityMode = true;

#if defined(Q_OS_ANDROID)
    startVoipService(true);
    saveConnectionState();
    updateServiceSelectedTalkgroup(m_talkgroup);
    updateServiceCurrentTalker(QString());
    updateServiceReceiveState(false, QString());
    updateServiceTransmitState(false);
#endif
    m_clientId = 0;
    m_udpSequence = 0;
    m_state = Connecting; m_connectionStatus = "Connecting to " + m_host + "...";
    emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
    updateServiceConnectionStatus(m_connectionStatus, false);
#endif
    m_tcpBuffer.clear();
    m_lastAudioSeq = 0;

    m_currentTalker.clear();
    setReceivingAudioState(false);
    resetAudioMeters();

    // Configure TCP socket for freeze cycle survival
    m_tcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption, true);
    m_tcpSocket->setSocketOption(QAbstractSocket::LowDelayOption, true);

    m_connectTimer->start(5000);
    m_tcpSocket->connectToHost(m_host, m_port);
}

void ReflectorClient::disconnectFromServer()
{
#if defined(Q_OS_ANDROID)
    updateServiceTransmitState(false);
    updateServiceReceiveState(false, QString());
    updateServiceCurrentTalker(QString());
    updateServiceConnectionStatus(QStringLiteral("Disconnected"), false);
    setServiceConnectionMonitoring(false);
    clearConnectionState();
#endif
    clearReconnectSchedule();
    resetReconnectBackoff();
    setWaitingForValidatedNetwork(false);
    resetProtocolLivenessWatchdog();
    m_ignoreNextSocketError = false;
    m_ignoreNextSocketDisconnect = false;

    // Explicitly cleanup audio resources when disconnecting
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "cleanup", Qt::QueuedConnection);
    }
    if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        if (m_state == Connecting) {
            m_ignoreNextSocketError = true;
            m_ignoreNextSocketDisconnect = true;
            m_tcpSocket->abort();
        } else {
            m_ignoreNextSocketDisconnect = true;
            m_tcpSocket->disconnectFromHost();
        }
    }
    transitionToDisconnectedState(QStringLiteral("Disconnected"), false);
}

void ReflectorClient::onTcpConnected()
{
    m_ignoreNextSocketDisconnect = false;
    m_ignoreNextSocketError = false;
    m_connectTimer->stop();
    m_state = Authenticating;
    m_connectionStatus = "Connected, authenticating...";

    qDebug() << "ReflectorClient::onTcpConnected - TCP connection established"
             << "Local address:" << m_tcpSocket->localAddress().toString() << ":" << m_tcpSocket->localPort()
             << "Peer address:" << m_tcpSocket->peerAddress().toString() << ":" << m_tcpSocket->peerPort()
             << "Host was:" << m_host << ":" << m_port;

    emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
    updateServiceConnectionStatus(m_connectionStatus, false);
    updateServiceReceiveState(false, QString());
    updateServiceTransmitState(false);
#endif

    QHostAddress::SpecialAddress bindAddr = QHostAddress::AnyIPv4;
    bool udpBound = m_udpSocket->bind(bindAddr, 0);
    qDebug() << "ReflectorClient::onTcpConnected - UDP socket bind result:" << udpBound
             << "UDP local port:" << m_udpSocket->localPort()
             << "UDP state:" << m_udpSocket->state();

    sendProtoVer();
}

void ReflectorClient::onTcpDisconnected()
{
    const bool ignoredDisconnect = m_ignoreNextSocketDisconnect;
    m_ignoreNextSocketDisconnect = false;

    if (ignoredDisconnect) {
        qDebug() << "ReflectorClient::onTcpDisconnected - ignoring expected TCP disconnect"
                 << "Host was:" << m_host << ":" << m_port;
        m_ignoreNextSocketError = false;
        return;
    }

    qWarning() << "ReflectorClient::onTcpDisconnected - TCP connection lost"
               << "Previous state:" << (m_state == Connected ? "Connected" : m_state == Authenticating ? "Authenticating" : "Other")
               << "TCP socket error:" << m_tcpSocket->error()
               << "TCP socket error string:" << m_tcpSocket->errorString()
               << "Host was:" << m_host << ":" << m_port;

    transitionToDisconnectedState(QStringLiteral("Disconnected"), true);
    scheduleReconnectAttempt(QStringLiteral("tcp socket disconnected"), false);
}

void ReflectorClient::onTcpError(QAbstractSocket::SocketError socketError)
{
    if (m_ignoreNextSocketError) {
        qDebug() << "ReflectorClient::onTcpError - ignoring expected socket error" << socketError;
        m_ignoreNextSocketError = false;
        return;
    }

    qWarning() << "ReflectorClient::onTcpError - TCP socket error occurred"
               << "Error code:" << socketError
               << "Error string:" << m_tcpSocket->errorString()
               << "Connection state:" << m_state
               << "Host:" << m_host << ":" << m_port;

    if (m_state != Disconnected) {
        m_ignoreNextSocketDisconnect = true;
        m_tcpSocket->abort();
        transitionToDisconnectedState(QStringLiteral("Connection failed"), true);
        scheduleReconnectAttempt(QStringLiteral("tcp socket error"), false);
    }
}

void ReflectorClient::onConnectTimeout()
{
    if (m_state != Disconnected) {
        m_ignoreNextSocketError = true;
        m_ignoreNextSocketDisconnect = true;
        m_tcpSocket->abort();
        transitionToDisconnectedState(QStringLiteral("Connection timeout"), true);
        scheduleReconnectAttempt(QStringLiteral("connection timeout"), false);
    }
}

void ReflectorClient::checkAndReconnect()
{
    ReconnectContext reconnectContext;
    const bool hasReconnectContext = resolveReconnectContext(reconnectContext);

    if (!m_tcpSocket) {
        qDebug() << "TCP socket is null, creating new connection...";
        if (hasReconnectContext) {
            scheduleReconnectAttempt(QStringLiteral("missing tcp socket"), true);
        } else {
            qWarning() << "Reconnect requested with no valid saved or in-memory connection profile";
        }
        return;
    }

    QAbstractSocket::SocketState state = m_tcpSocket->state();

    if (state == QAbstractSocket::UnconnectedState ||
        state == QAbstractSocket::ClosingState ||
        state == QAbstractSocket::BoundState ||
        m_state == Disconnected) {

        qWarning() << "TCP disconnection detected during freeze/unfreeze cycle. State:" << state << "m_state:" << m_state;

        if (hasReconnectContext) {
            qInfo() << "Scheduling reconnect after freeze-cycle disconnect";
            scheduleReconnectAttempt(QStringLiteral("freeze-cycle connection check"), false);
        } else {
            qWarning() << "Reconnect skipped because no valid connection profile is available"
                       << "hostEmpty=" << reconnectContext.host.isEmpty()
                       << "port=" << reconnectContext.port
                       << "authEmpty=" << reconnectContext.authKey.isEmpty()
                       << "callsignEmpty=" << reconnectContext.callsign.isEmpty();
        }
    } else if (state == QAbstractSocket::ConnectedState) {
        if (m_state == Connected) {
            sendHeartbeat();
        }
    } else {
        qDebug() << "TCP socket in intermediate state:" << state << "- monitoring...";
    }
}
