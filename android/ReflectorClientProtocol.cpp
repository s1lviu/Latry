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
#include "ReflectorProtocol.h"
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QDataStream>
#include <QtEndian>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>
#include <QMetaObject>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniObject>
#endif

namespace {
bool readLengthPrefixedUtf8(QDataStream &stream, QString &value)
{
    quint16 len = 0;
    stream >> len;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    QByteArray data(len, Qt::Uninitialized);
    if (len > 0 && stream.readRawData(data.data(), len) != len) {
        return false;
    }

    value = QString::fromUtf8(data);
    return true;
}
}

// --- Protocol Serialization ---

void ReflectorClient::sendFrame(const QByteArray &payload)
{
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (uint32_t)payload.size();
    frame.append(payload);
    m_tcpSocket->write(frame);
}

void ReflectorClient::sendProtoVer()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::PROTO_VER;
    stream << (quint16)Svxlink::Protocol::MAJOR_VER;
    stream << (quint16)Svxlink::Protocol::MINOR_VER;
    sendFrame(payload);
}

void ReflectorClient::sendAuthResponse(const QByteArray &hmac)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    QByteArray callsignData = m_callsign.toUtf8();
    stream << (quint16)Svxlink::MsgType::AUTH_RESPONSE;
    stream << (quint16)callsignData.size();
    stream.writeRawData(callsignData.constData(), callsignData.size());
    stream << (quint16)hmac.size();
    stream.writeRawData(hmac.constData(), hmac.size());
    sendFrame(payload);
}

void ReflectorClient::sendNodeInfo()
{
    QJsonObject nodeInfoJson = m_customNodeInfoJson;
    nodeInfoJson["sw"] = nodeInfoSoftwareName();
    nodeInfoJson["swVer"] = nodeInfoSoftwareVersion();
    nodeInfoJson["callsign"] = m_callsign;
    nodeInfoJson["tip"] = nodeInfoTipHtml();
    nodeInfoJson["Website"] = nodeInfoWebsite();
    QJsonDocument doc(nodeInfoJson);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::NODE_INFO;
    stream << (quint16)jsonData.size();
    stream.writeRawData(jsonData.constData(), jsonData.size());
    sendFrame(payload);
}

void ReflectorClient::sendSelectTG(quint32 talkgroup)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::SELECT_TG;
    stream << talkgroup;
    sendFrame(payload);
}

void ReflectorClient::sendTgMonitor(const QList<quint32> &talkgroups)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::TG_MONITOR;
    stream << static_cast<quint16>(talkgroups.size());
    for (quint32 talkgroup : talkgroups) {
        stream << talkgroup;
    }
    sendFrame(payload);
}

void ReflectorClient::sendHeartbeat()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::HEARTBEAT;
    sendFrame(payload);
}

// --- Message Handling ---

void ReflectorClient::handleAuthChallenge(QDataStream &stream)
{
    quint16 len = 0;
    stream >> len;
    QByteArray challenge(len, 0);
    stream.readRawData(challenge.data(), len);
    QByteArray hmac = QMessageAuthenticationCode::hash(challenge, m_authKey, QCryptographicHash::Sha1);
    sendAuthResponse(hmac);
}

void ReflectorClient::handleServerInfo(QDataStream &stream)
{
    quint16 reserved = 0;
    stream >> reserved >> m_clientId;
    m_state = Connected;
    resetReconnectBackoff();
    setWaitingForValidatedNetwork(false);
    resetProtocolLivenessWatchdog();

    qDebug() << "ReflectorClient::handleServerInfo - Authentication successful"
             << "Client ID:" << m_clientId
             << "Reserved field:" << reserved
             << "Target talkgroup:" << m_talkgroup
             << "TCP peer address for UDP:" << m_tcpSocket->peerAddress().toString();

    refreshConnectionStatus();
    emit selectedTalkgroupChanged();
#if defined(Q_OS_ANDROID)
    updateServiceSelectedTalkgroup(m_talkgroup);
#endif
    qInfo() << "Authenticated! ClientID:" << m_clientId;
    sendNodeInfo();
    sendSelectTG(m_talkgroup);
    sendTgMonitor(m_monitoredTalkgroups);
    if (m_talkgroup > 0) {
        resetTalkgroupSelectionTimer();
    } else {
        stopTalkgroupSelectionTimer();
    }
    m_heartbeatTimer->start(5000);

    // send initial UDP heartbeat to register our port immediately
    setupAudio();
    QByteArray datagram(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
    auto* header = reinterpret_cast<Svxlink::UdpMsgHeader*>(datagram.data());
    header->type = qToBigEndian((quint16)Svxlink::UdpMsgType::UDP_HEARTBEAT);
    header->clientId = qToBigEndian((quint16)m_clientId);
    header->sequenceNum = qToBigEndian(m_udpSequence++);

    qDebug() << "ReflectorClient::handleServerInfo - Sending initial UDP heartbeat, sequence:" << (m_udpSequence-1);
    sendUdpMessage(datagram);
#if defined(Q_OS_ANDROID)
    resumeAndroidPttAfterReconnectIfReady();
#endif
}

void ReflectorClient::handleTalkerStart(QDataStream &stream)
{
    quint32 tg = 0;
    if (stream.device()->bytesAvailable() >= static_cast<int>(sizeof(quint32)))
        stream >> tg;
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign == m_callsign) {
        if (!m_currentTalker.isEmpty()) {
            m_currentTalker.clear();
            emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
            updateServiceCurrentTalker(QString());
#endif
        }
        if (!m_currentTalkerName.isEmpty()) {
            m_currentTalkerName.clear();
            emit currentTalkerNameChanged();
        }
        return;
    }
    if (!shouldHandleTalkerStart(tg)) {
        return;
    }
    m_currentTalker = callsign;
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(m_currentTalker);
    if (m_isReceivingAudio) {
        updateServiceReceiveState(true, m_currentTalker);
    }
#endif
    m_currentTalkerName.clear();
    emit currentTalkerNameChanged();

    startNameLookup(callsign);
}

void ReflectorClient::handleTalkerStartV1(QDataStream &stream)
{
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign == m_callsign) {
        if (!m_currentTalker.isEmpty()) {
            m_currentTalker.clear();
            emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
            updateServiceCurrentTalker(QString());
#endif
        }
        if (!m_currentTalkerName.isEmpty()) {
            m_currentTalkerName.clear();
            emit currentTalkerNameChanged();
        }
        return;
    }
    m_currentTalker = callsign;
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(m_currentTalker);
    if (m_isReceivingAudio) {
        updateServiceReceiveState(true, m_currentTalker);
    }
#endif
    m_currentTalkerName.clear();
    emit currentTalkerNameChanged();

    startNameLookup(callsign);
}

void ReflectorClient::handleTalkerStop(QDataStream &stream)
{
    quint32 tg = 0;
    if (stream.device()->bytesAvailable() >= static_cast<int>(sizeof(quint32)))
        stream >> tg;
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign != m_currentTalker)
        return;
    m_currentTalker.clear();
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(QString());
    updateServiceReceiveState(false, QString());
#endif
    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }
}

void ReflectorClient::handleTalkerStopV1(QDataStream &stream)
{
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign != m_currentTalker)
        return;
    m_currentTalker.clear();
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(QString());
    updateServiceReceiveState(false, QString());
#endif
    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }
}

// --- TCP Message Dispatch ---

void ReflectorClient::onTcpReadyRead()
{
    m_tcpBuffer.append(m_tcpSocket->readAll());

    while (true) {
        if (m_tcpBuffer.size() < sizeof(uint32_t)) {
            break;
        }
        uint32_t payloadSize = qFromBigEndian<uint32_t>(m_tcpBuffer.constData());
        if (payloadSize > 1024 * 1024) {
            qWarning() << "Received excessively large frame size, disconnecting.";
            disconnectFromServer();
            return;
        }
        if (m_tcpBuffer.size() < (qint64)(sizeof(uint32_t) + payloadSize)) {
            break;
        }

        m_tcpBuffer.remove(0, sizeof(uint32_t));
        QByteArray payloadData = m_tcpBuffer.left(payloadSize);
        m_tcpBuffer.remove(0, payloadSize);

        QDataStream payloadStream(payloadData);
        payloadStream.setByteOrder(QDataStream::BigEndian);
        quint16 messageType;
        payloadStream >> messageType;

        switch(messageType) {
        case Svxlink::MsgType::PROTO_VER:
            qDebug() << "Received PROTO_VER from server. Waiting for challenge.";
            break;
        case Svxlink::MsgType::AUTH_CHALLENGE:
            qDebug() << "Received AUTH_CHALLENGE from server.";
            handleAuthChallenge(payloadStream);
            break;
        case Svxlink::MsgType::AUTH_OK:
            qDebug() << "Received AUTH_OK from server. Waiting for SERVER_INFO.";
            break;
        case Svxlink::MsgType::PROTO_VER_DOWNGRADE: {
            quint16 majorVer, minorVer;
            payloadStream >> majorVer >> minorVer;

            qWarning() << "Server requested protocol downgrade to" << majorVer << "." << minorVer;
            qWarning() << "Protocol downgrade not supported - disconnecting";

            m_connectionStatus = "Protocol version incompatible";
            emit connectionStatusChanged();
            m_state = Disconnected;
#if defined(Q_OS_ANDROID)
            updateServiceConnectionStatus(m_connectionStatus, false);
#endif
            break;
        }
        case Svxlink::MsgType::ERROR: {
            quint16 messageLen = 0;
            payloadStream >> messageLen;

            QByteArray errorMessage(messageLen, Qt::Uninitialized);
            payloadStream.readRawData(errorMessage.data(), messageLen);
            QString errorString = QString::fromLatin1(errorMessage);

            qWarning() << "Server error:" << errorString;
            m_connectionStatus = "Server error: " + errorString;
            emit connectionStatusChanged();

            if (errorString.contains("Access denied", Qt::CaseInsensitive) ||
                errorString.contains("Authentication", Qt::CaseInsensitive)) {
                qDebug() << "Clearing cached auth key due to authentication failure";
                m_authKey.clear();
            }

            m_state = Disconnected;
#if defined(Q_OS_ANDROID)
            updateServiceConnectionStatus(m_connectionStatus, false);
#endif
            break;
        }
        case Svxlink::MsgType::SERVER_INFO:
            qDebug() << "Received SERVER_INFO from server.";
            handleServerInfo(payloadStream);
            break;
        case Svxlink::MsgType::HEARTBEAT:
            noteInboundProtocolHeartbeat();
            break;
        case Svxlink::MsgType::NODE_LIST: {
            quint16 nodeCount = 0;
            payloadStream >> nodeCount;
            if (payloadStream.status() != QDataStream::Ok) {
                qWarning() << "Malformed NODE_LIST frame: failed to read node count";
                break;
            }

            qDebug() << "Received NODE_LIST with" << nodeCount << "nodes";
            QStringList nodes;

            for (int i = 0; i < nodeCount; i++) {
                QString callsign;
                if (!readLengthPrefixedUtf8(payloadStream, callsign)) {
                    qWarning() << "Malformed NODE_LIST frame: failed to read node" << i;
                    nodes.clear();
                    break;
                }
                if (!callsign.isEmpty()) {
                    nodes.append(callsign);
                }
            }

            if (!nodes.isEmpty()) {
                emit connectedNodesChanged(nodes);
                qDebug() << "Connected nodes:" << nodes;
            }
            break;
        }
        case Svxlink::MsgType::NODE_JOINED: {
            QString callsign;
            if (!readLengthPrefixedUtf8(payloadStream, callsign)) {
                qWarning() << "Malformed NODE_JOINED frame";
                break;
            }

            qDebug() << "Node joined:" << callsign;
            emit nodeJoined(callsign);
            break;
        }
        case Svxlink::MsgType::NODE_LEFT: {
            QString callsign;
            if (!readLengthPrefixedUtf8(payloadStream, callsign)) {
                qWarning() << "Malformed NODE_LEFT frame";
                break;
            }

            qDebug() << "Node left:" << callsign;
            emit nodeLeft(callsign);
            break;
        }
        case Svxlink::MsgType::TG_MONITOR: {
            qWarning() << "Received unexpected inbound TG_MONITOR frame from server; ignoring";
            break;
        }
        case Svxlink::MsgType::REQUEST_QSY: {
            quint32 newTalkgroup;
            payloadStream >> newTalkgroup;

            qDebug() << "QSY requested to talkgroup:" << newTalkgroup;
            selectTalkgroupInternal(newTalkgroup, TalkgroupSelectionOrigin::RequestQsy);
            emit qsyRequested(newTalkgroup);
            break;
        }
        case Svxlink::MsgType::STATE_EVENT: {
            quint16 srcLen, nameLen, msgLen;
            payloadStream >> srcLen >> nameLen >> msgLen;

            QByteArray srcData(srcLen, Qt::Uninitialized);
            QByteArray nameData(nameLen, Qt::Uninitialized);
            QByteArray msgData(msgLen, Qt::Uninitialized);

            payloadStream.readRawData(srcData.data(), srcLen);
            payloadStream.readRawData(nameData.data(), nameLen);
            payloadStream.readRawData(msgData.data(), msgLen);

            QString src = QString::fromUtf8(srcData);
            QString name = QString::fromUtf8(nameData);
            QString message = QString::fromUtf8(msgData);

            qDebug() << "State event from" << src << ":" << name << "=" << message;
            emit stateEventReceived(src, name, message);
            break;
        }
        case Svxlink::MsgType::SIGNAL_STRENGTH: {
            float rxSignal, rxSqlOpen;
            QByteArray callsignData(20, Qt::Uninitialized);

            payloadStream >> rxSignal >> rxSqlOpen;
            payloadStream.readRawData(callsignData.data(), 20);

            QString callsign = QString::fromLatin1(callsignData).trimmed();

            qDebug() << "Signal strength from" << callsign << "- RX:" << rxSignal << "SQL:" << rxSqlOpen;
            emit signalStrengthReceived(callsign, rxSignal, rxSqlOpen);
            break;
        }
        case Svxlink::MsgType::TX_STATUS: {
            quint8 txState;
            QByteArray callsignData(20, Qt::Uninitialized);

            payloadStream >> txState;
            payloadStream.readRawData(callsignData.data(), 20);

            QString callsign = QString::fromLatin1(callsignData).trimmed();
            bool isTransmitting = (txState != 0);

            qDebug() << "TX status from" << callsign << ":" << (isTransmitting ? "ON" : "OFF");
            emit txStatusReceived(callsign, isTransmitting);
            break;
        }
        case Svxlink::MsgType::TALKER_START: {
            QDataStream testStream(payloadData.mid(sizeof(quint16)));
            testStream.setByteOrder(QDataStream::BigEndian);
            bool v2 = false;
            if (testStream.device()->bytesAvailable() >= static_cast<int>(sizeof(quint32)+sizeof(quint16))) {
                quint32 tg;
                quint16 len;
                testStream >> tg;
                testStream >> len;
                if (testStream.device()->bytesAvailable() >= len)
                    v2 = true;
            }
            if (v2)
                handleTalkerStart(payloadStream);
            else
                handleTalkerStartV1(testStream);
            break;
        }
        case Svxlink::MsgType::TALKER_STOP: {
            QDataStream testStream(payloadData.mid(sizeof(quint16)));
            testStream.setByteOrder(QDataStream::BigEndian);
            bool v2 = false;
            if (testStream.device()->bytesAvailable() >= static_cast<int>(sizeof(quint32)+sizeof(quint16))) {
                quint32 tg;
                quint16 len;
                testStream >> tg;
                testStream >> len;
                if (testStream.device()->bytesAvailable() >= len)
                    v2 = true;
            }
            if (v2)
                handleTalkerStop(payloadStream);
            else
                handleTalkerStopV1(testStream);
            break;
        }
        default:
            qWarning() << "Received unhandled TCP message, type:" << messageType
                       << "Payload size:" << payloadSize
                       << "Connection state:" << m_state
                       << "Known types: HEARTBEAT(1), PROTO_VER(5), PROTO_VER_DOWNGRADE(6), AUTH_CHALLENGE(10), AUTH_OK(12), ERROR(13), SERVER_INFO(100), NODE_LIST(101), NODE_JOINED(102), NODE_LEFT(103), TALKER_START(104), TALKER_STOP(105), SELECT_TG(106), TG_MONITOR(107), REQUEST_QSY(109), STATE_EVENT(110), NODE_INFO(111), SIGNAL_STRENGTH(112), TX_STATUS(113)";

            if (payloadSize <= 64) {
                qDebug() << "Payload hex dump:" << payloadData.toHex(' ');
            }
            break;
        }
    }
}

// --- Name Lookup ---

void ReflectorClient::startNameLookup(const QString &callsign)
{
    if (m_nameReply) {
        disconnect(m_nameReply, nullptr, this, nullptr);
        m_nameReply->abort();
        QMetaObject::invokeMethod(m_nameReply, "deleteLater", Qt::QueuedConnection);
        m_nameReply = nullptr;
    }

    QUrl url(QStringLiteral("https://cs.latry.app/?callsign=%1").arg(callsign));
    QNetworkRequest req(url);
    req.setRawHeader("X-Api-Key",
                     QByteArrayLiteral("d5a34df7f2fc24c6a487697fdc242e984ecedfd1f4329ba268f1dd4736a23b20"));
    m_nameReply = m_networkManager->get(req);
    connect(m_nameReply, &QNetworkReply::finished, this, &ReflectorClient::onNameLookupFinished);
}

void ReflectorClient::onNameLookupFinished()
{
    if (!m_nameReply)
        return;

    QNetworkReply* reply = m_nameReply;
    m_nameReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        QMetaObject::invokeMethod(reply, "deleteLater", Qt::QueuedConnection);
        return;
    }

    QByteArray data = reply->readAll();
    QMetaObject::invokeMethod(reply, "deleteLater", Qt::QueuedConnection);

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        QString fname = obj.value("fname").toString();
        if (!fname.isEmpty()) {
            m_currentTalkerName = fname;
            emit currentTalkerNameChanged();
        }
    }
}
