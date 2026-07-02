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
#include <QDataStream>
#include <QtEndian>
#include <QHostAddress>
#include <QDebug>
#include <QMetaObject>

namespace {
QString udpMessageTypeName(quint16 messageType)
{
    switch (messageType) {
    case Svxlink::UdpMsgType::UDP_HEARTBEAT:
        return QStringLiteral("UDP_HEARTBEAT");
    case Svxlink::UdpMsgType::UDP_AUDIO:
        return QStringLiteral("UDP_AUDIO");
    case Svxlink::UdpMsgType::UDP_FLUSH_SAMPLES:
        return QStringLiteral("UDP_FLUSH_SAMPLES");
    case Svxlink::UdpMsgType::UDP_ALL_SAMPLES_FLUSHED:
        return QStringLiteral("UDP_ALL_SAMPLES_FLUSHED");
    case Svxlink::UdpMsgType::UDP_SIGNAL_STRENGTH:
        return QStringLiteral("UDP_SIGNAL_STRENGTH");
    default:
        return QStringLiteral("UDP_UNKNOWN");
    }
}

bool shouldLogInboundUdpMessage(quint16 messageType)
{
    return messageType != Svxlink::UdpMsgType::UDP_HEARTBEAT
            && messageType != Svxlink::UdpMsgType::UDP_AUDIO;
}

bool shouldLogOutboundUdpMessage(quint16 messageType)
{
    return messageType != Svxlink::UdpMsgType::UDP_HEARTBEAT
            && messageType != Svxlink::UdpMsgType::UDP_AUDIO;
}

quint16 datagramMessageType(const QByteArray &datagram)
{
    if (datagram.size() < static_cast<int>(sizeof(Svxlink::UdpMsgHeader))) {
        return 0;
    }

    const auto *header = reinterpret_cast<const Svxlink::UdpMsgHeader*>(datagram.constData());
    return qFromBigEndian(header->type);
}
}

void ReflectorClient::onUdpReadyRead()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(datagram.data(), datagram.size());

        const auto* header = reinterpret_cast<const Svxlink::UdpMsgHeader*>(datagram.constData());

        uint16_t messageType = qFromBigEndian(header->type);
        if (shouldLogInboundUdpMessage(messageType)) {
            qDebug() << "ReflectorClient::onUdpReadyRead - Processing"
                     << udpMessageTypeName(messageType);
        }

        switch (messageType) {
        case Svxlink::UdpMsgType::UDP_HEARTBEAT: {
            break;
        }
        case Svxlink::UdpMsgType::UDP_AUDIO: {
            const auto* msg = static_cast<const Svxlink::MsgUdpAudio*>(header);
            quint16 seq = qFromBigEndian(header->sequenceNum);
            int opusDataLen = qFromBigEndian(msg->audioLen);

            if (m_audioEngine && opusDataLen > 0) {
                QByteArray audioData(reinterpret_cast<const char*>(msg->audioData), opusDataLen);
                QMetaObject::invokeMethod(m_audioEngine, "processReceivedAudio", Qt::QueuedConnection,
                                          Q_ARG(QByteArray, audioData), Q_ARG(quint16, seq));

                if (!m_isReceivingAudio) {
                    setReceivingAudioState(true);
                }
                m_audioTimeoutTimer->start();
            }

            m_lastAudioSeq = seq;
            break;
        }
        case Svxlink::UdpMsgType::UDP_FLUSH_SAMPLES:
            m_lastAudioSeq = 0;
            if (m_audioEngine) {
                QMetaObject::invokeMethod(m_audioEngine, "flushAudioBuffers", Qt::QueuedConnection);
            }
            if (m_isReceivingAudio) {
                setReceivingAudioState(false);
            }
            break;
        case Svxlink::UdpMsgType::UDP_ALL_SAMPLES_FLUSHED:
            qDebug() << "Received UDP all samples flushed";
            if (m_audioEngine) {
                QMetaObject::invokeMethod(m_audioEngine, "allSamplesFlushed", Qt::QueuedConnection);
            }
            break;
        case Svxlink::UdpMsgType::UDP_SIGNAL_STRENGTH: {
            const auto* msg = static_cast<const Svxlink::MsgUdpSignalStrength*>(header);
            float rxSignal = msg->rx_signal_strength;
            float rxSqlOpen = msg->rx_sql_open;

            QByteArray callsignData(msg->callsign, 20);
            QString callsign = QString::fromLatin1(callsignData).trimmed();

            qDebug() << "UDP Signal strength from" << callsign << "- RX:" << rxSignal << "SQL:" << rxSqlOpen;
            emit signalStrengthReceived(callsign, rxSignal, rxSqlOpen);
            break;
        }
        default:
            qWarning() << "Received unhandled UDP message, type:" << messageType
                       << "Known UDP types: UDP_HEARTBEAT(1), UDP_AUDIO(101), UDP_FLUSH_SAMPLES(102), UDP_ALL_SAMPLES_FLUSHED(103), UDP_SIGNAL_STRENGTH(104)";
            break;
        }
    }
}

void ReflectorClient::onHeartbeatTimer()
{
    if (m_state == Connected) {
        sendHeartbeat();
        QByteArray datagram(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
        auto* udpBeat = reinterpret_cast<Svxlink::UdpMsgHeader*>(datagram.data());
        udpBeat->type = qToBigEndian((quint16)Svxlink::UdpMsgType::UDP_HEARTBEAT);
        udpBeat->clientId = qToBigEndian((quint16)m_clientId);
        udpBeat->sequenceNum = qToBigEndian(m_udpSequence++);
        sendUdpMessage(datagram);
    }
}

void ReflectorClient::onAudioDataEncoded(const QByteArray &encodedData)
{
    if (!m_pttActive) {
        qDebug() << "ReflectorClient::onAudioDataEncoded - PTT not active, ignoring encoded data";
        return;
    }

    QByteArray datagram(sizeof(Svxlink::UdpMsgHeader) + sizeof(quint16) + encodedData.size(), Qt::Uninitialized);
    QDataStream ds(&datagram, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << (quint16)Svxlink::UdpMsgType::UDP_AUDIO
       << (quint16)m_clientId
       << (quint16)m_udpSequence++
       << (quint16)encodedData.size();
    ds.writeRawData(encodedData.constData(), encodedData.size());
    sendUdpMessage(datagram);
}

void ReflectorClient::sendUdpMessage(const QByteArray &datagram)
{
    const quint16 messageType = datagramMessageType(datagram);
    const QString typeName = udpMessageTypeName(messageType);

    if (m_udpSocket->state() == QAbstractSocket::BoundState) {
        QHostAddress addr = m_tcpSocket->peerAddress();

        if (!addr.isNull()) {
            qint64 bytesWritten = m_udpSocket->writeDatagram(datagram, addr, m_port);
            if (bytesWritten < 0) {
                qWarning() << "ReflectorClient::sendUdpMessage - Failed to send" << typeName
                           << "to" << addr.toString() << ":" << m_port
                           << "error:" << m_udpSocket->errorString();
            } else if (shouldLogOutboundUdpMessage(messageType)) {
                qDebug() << "ReflectorClient::sendUdpMessage - Sent" << typeName
                         << "bytes:" << bytesWritten
                         << "to" << addr.toString() << ":" << m_port;
            }
        } else {
            qWarning() << "ReflectorClient::sendUdpMessage - No valid TCP peer address available"
                       << "for" << typeName
                       << "TCP socket state:" << m_tcpSocket->state()
                       << "TCP socket peer:" << m_tcpSocket->peerName()
                       << "Host was:" << m_host;
        }
    } else {
        qWarning() << "ReflectorClient::sendUdpMessage - UDP socket not bound for" << typeName
                   << "state:" << m_udpSocket->state();
    }
}
