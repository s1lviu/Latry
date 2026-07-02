#include <QtTest>

#include "ReflectorProtocol.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtEndian>

class ReflectorLiveIntegrationTest : public QObject
{
    Q_OBJECT

private:
    struct Config {
        QString host;
        quint16 port = 5300;
        QString callsign;
        QByteArray authKey;
        quint32 talkgroup = 9;
        int timeoutMs = 10000;
    };

private slots:
    void handshakeAgainstConfiguredReflector();

private:
    Config loadConfig() const;
    static QByteArray framePayload(const QByteArray &payload);
    static quint16 payloadType(const QByteArray &payload);
    static QString decodeErrorPayload(const QByteArray &payload);
    static QByteArray protoVerPayload();
    static QByteArray authResponsePayload(const QString &callsign,
                                          const QByteArray &authKey,
                                          const QByteArray &challenge);
    static QByteArray nodeInfoPayload(const QString &callsign);
    static QByteArray selectTalkgroupPayload(quint32 talkgroup);
    static QByteArray tgMonitorPayload(quint32 talkgroup);
    static QByteArray heartbeatPayload();
    static QByteArray udpHeartbeatPayload(quint16 clientId, quint16 sequence);
    QByteArray readFrame(QTcpSocket &socket, int timeoutMs);

    QByteArray m_tcpBuffer;
};

ReflectorLiveIntegrationTest::Config ReflectorLiveIntegrationTest::loadConfig() const
{
    Config config;
    config.host = qEnvironmentVariable("LATRY_LIVE_REFLECTOR_HOST", QStringLiteral("reflector-test.brainic.ro"));
    config.callsign = qEnvironmentVariable("LATRY_LIVE_REFLECTOR_CALLSIGN");
    config.authKey = qEnvironmentVariable("LATRY_LIVE_REFLECTOR_AUTH_KEY").toUtf8();

    bool ok = false;
    const int configuredPort = qEnvironmentVariableIntValue("LATRY_LIVE_REFLECTOR_PORT", &ok);
    if (ok && configuredPort > 0 && configuredPort <= 65535) {
        config.port = quint16(configuredPort);
    }

    ok = false;
    const int configuredTalkgroup = qEnvironmentVariableIntValue("LATRY_LIVE_REFLECTOR_TALKGROUP", &ok);
    if (ok && configuredTalkgroup >= 0) {
        config.talkgroup = quint32(configuredTalkgroup);
    }

    ok = false;
    const int configuredTimeout = qEnvironmentVariableIntValue("LATRY_LIVE_REFLECTOR_TIMEOUT_MS", &ok);
    if (ok && configuredTimeout > 0) {
        config.timeoutMs = configuredTimeout;
    }

    return config;
}

QByteArray ReflectorLiveIntegrationTest::framePayload(const QByteArray &payload)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(payload.size());
    frame.append(payload);
    return frame;
}

quint16 ReflectorLiveIntegrationTest::payloadType(const QByteArray &payload)
{
    if (payload.size() < int(sizeof(quint16))) {
        return 0;
    }

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);
    quint16 type = 0;
    stream >> type;
    return type;
}

QString ReflectorLiveIntegrationTest::decodeErrorPayload(const QByteArray &payload)
{
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    quint16 type = 0;
    quint16 messageLen = 0;
    stream >> type >> messageLen;
    QByteArray message(messageLen, Qt::Uninitialized);
    if (messageLen > 0) {
        stream.readRawData(message.data(), messageLen);
    }
    return QString::fromUtf8(message);
}

QByteArray ReflectorLiveIntegrationTest::protoVerPayload()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::PROTO_VER);
    stream << quint16(Svxlink::Protocol::MAJOR_VER);
    stream << quint16(Svxlink::Protocol::MINOR_VER);
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::authResponsePayload(const QString &callsign,
                                                             const QByteArray &authKey,
                                                             const QByteArray &challenge)
{
    const QByteArray digest = QMessageAuthenticationCode::hash(
        challenge, authKey, QCryptographicHash::Sha1);
    const QByteArray callsignData = callsign.toUtf8();

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::AUTH_RESPONSE);
    stream << quint16(callsignData.size());
    stream.writeRawData(callsignData.constData(), callsignData.size());
    stream << quint16(digest.size());
    stream.writeRawData(digest.constData(), digest.size());
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::nodeInfoPayload(const QString &callsign)
{
    const QJsonObject nodeInfo{
        {QStringLiteral("sw"), QStringLiteral("Latry Integration Test")},
        {QStringLiteral("swVer"), QStringLiteral("1")},
        {QStringLiteral("callsign"), callsign},
        {QStringLiteral("tip"), QStringLiteral("<b>Integration Test</b>")},
        {QStringLiteral("Website"), QStringLiteral("https://brainic.ro")}
    };
    const QByteArray json = QJsonDocument(nodeInfo).toJson(QJsonDocument::Compact);

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::NODE_INFO);
    stream << quint16(json.size());
    stream.writeRawData(json.constData(), json.size());
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::selectTalkgroupPayload(quint32 talkgroup)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::SELECT_TG);
    stream << talkgroup;
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::tgMonitorPayload(quint32 talkgroup)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::TG_MONITOR);
    stream << quint16(1);
    stream << talkgroup;
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::heartbeatPayload()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(Svxlink::MsgType::HEARTBEAT);
    return payload;
}

QByteArray ReflectorLiveIntegrationTest::udpHeartbeatPayload(quint16 clientId, quint16 sequence)
{
    QByteArray datagram(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
    auto *header = reinterpret_cast<Svxlink::UdpMsgHeader *>(datagram.data());
    header->type = qToBigEndian(quint16(Svxlink::UdpMsgType::UDP_HEARTBEAT));
    header->clientId = qToBigEndian(clientId);
    header->sequenceNum = qToBigEndian(sequence);
    return datagram;
}

QByteArray ReflectorLiveIntegrationTest::readFrame(QTcpSocket &socket, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (m_tcpBuffer.size() >= int(sizeof(quint32))) {
            const quint32 payloadSize = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(m_tcpBuffer.constData()));
            if (payloadSize > 1024 * 1024) {
                QTest::qFail("Received oversized TCP frame from reflector", __FILE__, __LINE__);
                return {};
            }

            if (m_tcpBuffer.size() >= int(sizeof(quint32) + payloadSize)) {
                m_tcpBuffer.remove(0, sizeof(quint32));
                const QByteArray payload = m_tcpBuffer.left(payloadSize);
                m_tcpBuffer.remove(0, payloadSize);
                return payload;
            }
        }

        m_tcpBuffer.append(socket.readAll());
        if (m_tcpBuffer.size() >= int(sizeof(quint32))) {
            continue;
        }

        const int remainingMs = qMax(1, timeoutMs - int(timer.elapsed()));
        if (!socket.waitForReadyRead(remainingMs)) {
            break;
        }
        m_tcpBuffer.append(socket.readAll());
    }

    return {};
}

void ReflectorLiveIntegrationTest::handshakeAgainstConfiguredReflector()
{
    const QString enabled = qEnvironmentVariable("LATRY_ENABLE_LIVE_REFLECTOR_TESTS").trimmed().toLower();
    if (!(enabled == QLatin1String("1") || enabled == QLatin1String("true") || enabled == QLatin1String("yes"))) {
        QSKIP("Live reflector integration test is disabled. Set LATRY_ENABLE_LIVE_REFLECTOR_TESTS=1 to run it.");
    }

    const Config config = loadConfig();
    if (config.host.isEmpty() || config.callsign.isEmpty() || config.authKey.isEmpty()) {
        QSKIP("Missing live reflector configuration. Set LATRY_LIVE_REFLECTOR_CALLSIGN and LATRY_LIVE_REFLECTOR_AUTH_KEY.");
    }

    m_tcpBuffer.clear();

    QTcpSocket tcpSocket;
    tcpSocket.setProxy(QNetworkProxy::NoProxy);

    QUdpSocket udpSocket;
    QVERIFY2(udpSocket.bind(QHostAddress::AnyIPv4, 0), qPrintable(udpSocket.errorString()));

    tcpSocket.connectToHost(config.host, config.port);
    QVERIFY2(tcpSocket.waitForConnected(config.timeoutMs), qPrintable(tcpSocket.errorString()));

    tcpSocket.write(framePayload(protoVerPayload()));
    QVERIFY2(tcpSocket.waitForBytesWritten(config.timeoutMs), qPrintable(tcpSocket.errorString()));

    QByteArray challenge;
    {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < config.timeoutMs && challenge.isEmpty()) {
            const QByteArray payload = readFrame(tcpSocket, qMax(1, config.timeoutMs - int(timer.elapsed())));
            QVERIFY2(!payload.isEmpty(), "Timed out waiting for AUTH_CHALLENGE");

            switch (payloadType(payload)) {
            case Svxlink::MsgType::PROTO_VER:
            case Svxlink::MsgType::HEARTBEAT:
                break;
            case Svxlink::MsgType::AUTH_CHALLENGE: {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                quint16 type = 0;
                quint16 len = 0;
                stream >> type >> len;
                challenge.resize(len);
                if (len > 0) {
                    stream.readRawData(challenge.data(), len);
                }
                break;
            }
            case Svxlink::MsgType::ERROR:
                QFAIL(qPrintable(QStringLiteral("Server rejected pre-auth handshake: %1").arg(decodeErrorPayload(payload))));
            case Svxlink::MsgType::PROTO_VER_DOWNGRADE:
                QFAIL("Server requested unsupported protocol downgrade");
            default:
                break;
            }
        }
    }

    QCOMPARE(challenge.size(), Svxlink::Protocol::CHALLENGE_LEN);

    tcpSocket.write(framePayload(authResponsePayload(config.callsign, config.authKey, challenge)));
    QVERIFY2(tcpSocket.waitForBytesWritten(config.timeoutMs), qPrintable(tcpSocket.errorString()));

    bool sawAuthOk = false;
    bool sawServerInfo = false;
    quint16 clientId = 0;
    {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < config.timeoutMs && !sawServerInfo) {
            const QByteArray payload = readFrame(tcpSocket, qMax(1, config.timeoutMs - int(timer.elapsed())));
            QVERIFY2(!payload.isEmpty(), "Timed out waiting for SERVER_INFO");

            switch (payloadType(payload)) {
            case Svxlink::MsgType::AUTH_OK:
                sawAuthOk = true;
                break;
            case Svxlink::MsgType::SERVER_INFO: {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                quint16 type = 0;
                quint16 reserved = 0;
                stream >> type >> reserved >> clientId;
                Q_UNUSED(reserved)
                sawServerInfo = true;
                break;
            }
            case Svxlink::MsgType::NODE_LIST:
            case Svxlink::MsgType::HEARTBEAT:
                break;
            case Svxlink::MsgType::ERROR:
                QFAIL(qPrintable(QStringLiteral("Authentication failed: %1").arg(decodeErrorPayload(payload))));
            default:
                break;
            }
        }
    }

    QVERIFY2(sawServerInfo, "Expected SERVER_INFO after authentication");
    QVERIFY2(clientId != 0, "Expected non-zero client id from reflector");
    if (!sawAuthOk) {
        qWarning("AUTH_OK was not observed before SERVER_INFO; continuing because SERVER_INFO confirms auth success.");
    }

    tcpSocket.write(framePayload(nodeInfoPayload(config.callsign)));
    tcpSocket.write(framePayload(selectTalkgroupPayload(config.talkgroup)));
    tcpSocket.write(framePayload(tgMonitorPayload(config.talkgroup)));
    tcpSocket.write(framePayload(heartbeatPayload()));
    QVERIFY2(tcpSocket.waitForBytesWritten(config.timeoutMs), qPrintable(tcpSocket.errorString()));

    const QByteArray udpHeartbeat = udpHeartbeatPayload(clientId, 0);
    const qint64 udpBytes = udpSocket.writeDatagram(udpHeartbeat, tcpSocket.peerAddress(), config.port);
    QCOMPARE(udpBytes, qint64(udpHeartbeat.size()));

    // The reflector may send a node list or heartbeat. The important part here is
    // that it does not immediately reject the authenticated session after post-auth frames.
    QElapsedTimer settleTimer;
    settleTimer.start();
    while (settleTimer.elapsed() < 1500) {
        if (!tcpSocket.waitForReadyRead(150)) {
            continue;
        }

        const QByteArray payload = readFrame(tcpSocket, 500);
        if (payload.isEmpty()) {
            continue;
        }

        if (payloadType(payload) == Svxlink::MsgType::ERROR) {
            QFAIL(qPrintable(QStringLiteral("Server rejected post-auth session setup: %1").arg(decodeErrorPayload(payload))));
        }
    }

    QVERIFY2(tcpSocket.state() == QAbstractSocket::ConnectedState
                 || tcpSocket.state() == QAbstractSocket::ClosingState,
             qPrintable(tcpSocket.errorString()));

    tcpSocket.disconnectFromHost();
    if (tcpSocket.state() != QAbstractSocket::UnconnectedState) {
        QVERIFY2(tcpSocket.waitForDisconnected(3000)
                     || tcpSocket.state() == QAbstractSocket::UnconnectedState,
                 qPrintable(tcpSocket.errorString()));
    }
}

QTEST_GUILESS_MAIN(ReflectorLiveIntegrationTest)

#include "tst_reflector_live_integration.moc"
