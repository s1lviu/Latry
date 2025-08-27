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
#include <QAudioDevice>
#include <QMediaDevices>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>
#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QtCore/QJniObject>
#  include <QFuture>
#  include <QFutureWatcher>
#endif
#include <QDebug>
#include <QDataStream>
#include <QtEndian>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHostAddress>
#include <QHostInfo>
#include <algorithm>
#include <QStringList>
#include <QTime>
#include <QDateTime>

#if defined(Q_OS_ANDROID)
#include <jni.h>
#include <android/log.h>
#include <QJniEnvironment>

// JNI callback declarations
extern "C" {
    // Forward declarations - MUST be before JNI_OnLoad
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusLost(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusGained(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityPaused(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityResumed(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped(JNIEnv *, jclass);
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection(JNIEnv *, jclass);
    
    // JNI_OnLoad - CRITICAL for Qt 6.9 Android JNI method registration
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
        Q_UNUSED(vm)
        Q_UNUSED(reserved)
        
        QJniEnvironment env;
        if (!env.isValid()) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to get JNI environment");
            return JNI_ERR;
        }
        
        // Register native methods for LatryActivity
        const JNINativeMethod activityMethods[] = {
            {"notifyAudioFocusLost", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusLost)},
            {"notifyAudioFocusPaused", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused)},
            {"notifyAudioFocusGained", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyAudioFocusGained)},
            {"notifyActivityPaused", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyActivityPaused)},
            {"notifyActivityResumed", "()V", reinterpret_cast<void*>(Java_yo6say_latry_LatryActivity_notifyActivityResumed)}
        };
        
        // Register native methods for VoipBackgroundService
        const JNINativeMethod serviceMethods[] = {
            {"notifyServiceStarted", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted)},
            {"notifyServiceStopped", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped)},
            {"notifyCheckConnection", "()V", reinterpret_cast<void*>(Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection)}
        };
        
        // Register methods for LatryActivity - use explicit size instead of sizeof
        if (!env.registerNativeMethods("yo6say/latry/LatryActivity", activityMethods, 5)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register LatryActivity native methods");
            return JNI_ERR;
        }
        
        // Register methods for VoipBackgroundService - use explicit size instead of sizeof
        if (!env.registerNativeMethods("yo6say/latry/VoipBackgroundService", serviceMethods, 3)) {
            __android_log_print(ANDROID_LOG_ERROR, "ReflectorClient", "Failed to register VoipBackgroundService native methods");
            return JNI_ERR;
        }
        
        __android_log_print(ANDROID_LOG_INFO, "ReflectorClient", "JNI native methods registered successfully");
        return JNI_VERSION_1_6;
    }
    // Mute functionality removed
}
#endif

ReflectorClient* ReflectorClient::instance()
{
    // Ensure QCoreApplication exists before creating Qt objects
    if (!QCoreApplication::instance()) {
        qWarning() << "ReflectorClient::instance() called before QCoreApplication created";
        return nullptr;
    }
    
    static ReflectorClient client;
    return &client;
}

ReflectorClient::ReflectorClient(QObject *parent) : QObject{parent},
    m_state(Disconnected),
    m_connectionStatus("Disconnected"),
    m_pttActive(false),
    m_audioReady(false),
    m_port(0),
    m_talkgroup(0),
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
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    m_audioTimeoutTimer = new QTimer(this);
    m_audioTimeoutTimer->setSingleShot(true);
    m_audioTimeoutTimer->setInterval(3000); // 3 second timeout
    m_networkManager = new QNetworkAccessManager(this);


    connect(m_tcpSocket, &QTcpSocket::connected, this, &ReflectorClient::onTcpConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ReflectorClient::onTcpDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ReflectorClient::onTcpReadyRead);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &ReflectorClient::onUdpReadyRead);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ReflectorClient::onHeartbeatTimer);
    connect(m_txTimer, &QTimer::timeout, this, &ReflectorClient::onTxTimerTimeout);
    connect(m_connectTimer, &QTimer::timeout, this, &ReflectorClient::onConnectTimeout);
    connect(m_audioTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_isReceivingAudio) {
            qDebug() << "Audio timeout - stopping receive indicator";
            m_isReceivingAudio = false;
            emit isReceivingAudioChanged();
        }
    });
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &ReflectorClient::onTcpError);
    m_txTimer->setInterval(1000);

    // Initialize audio engine and thread
    initializeAudioEngine();

#if defined(Q_OS_ANDROID)
    // JNI callbacks are handled through Qt's JNI system
    // The native methods are registered automatically when the Java methods are called
#endif

#if defined(Q_OS_IOS)
    // Connect iOS VoIP handler signals to ReflectorClient
    IOSVoIPHandler* iosHandler = IOSVoIPHandler::instance();
    connect(iosHandler, &IOSVoIPHandler::audioSessionInterrupted, 
            this, &ReflectorClient::notifyIOSAudioSessionInterrupted);
    connect(iosHandler, &IOSVoIPHandler::audioSessionResumed, 
            this, &ReflectorClient::notifyIOSAudioSessionResumed);
    connect(iosHandler, &IOSVoIPHandler::backgroundTaskExpired, 
            this, &ReflectorClient::notifyIOSBackgroundTaskExpired);
    qDebug() << "iOS VoIP handler connected to ReflectorClient";
#endif
    
    // Ensure UI gets the initial state values - delay to ensure QML is ready
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
    });
}

ReflectorClient::~ReflectorClient()
{
    if (m_audioThread && m_audioThread->isRunning()) {
        // AudioEngine will handle its own cleanup
        m_audioThread->quit();
        m_audioThread->wait();
    }
    if (m_txTimer) {
        m_txTimer->stop();
        m_txSeconds = 0;
        emit txTimeStringChanged();
    }
    // Stop any ongoing connection attempt
    m_connectTimer->stop();
    if (m_nameReply) {
        m_nameReply->abort();
        delete m_nameReply;  // Direct deletion is safe in destructor during shutdown
        m_nameReply = nullptr;
    }
    
#if defined(Q_OS_ANDROID)
    stopVoipService();
    clearConnectionState();
#endif
}

void ReflectorClient::setupAudio()
{
    if (m_audioEngine) {
        // Setup audio through the AudioEngine
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
    
    // Initialize AudioEngine with current mic gain setting
    QMetaObject::invokeMethod(m_audioEngine, "setMicGainDb", Qt::QueuedConnection, 
                              Q_ARG(double, m_micGainDb));
    
    // Connect signals from AudioEngine to ReflectorClient
    connect(m_audioEngine, &AudioEngine::audioReadyChanged, this, [this](bool ready) {
        m_audioReady = ready;
        emit audioReadyChanged();
    });
    
    connect(m_audioEngine, &AudioEngine::audioDataEncoded, this, &ReflectorClient::onAudioDataEncoded);
    connect(m_audioEngine, &AudioEngine::audioSetupFinished, this, &ReflectorClient::onAudioSetupFinished);
    
    // Connect Android audio focus signals
    connect(this, &ReflectorClient::audioFocusLost, m_audioEngine, &AudioEngine::onAudioFocusLost);
    connect(this, &ReflectorClient::audioFocusPaused, m_audioEngine, &AudioEngine::onAudioFocusPaused);
    connect(this, &ReflectorClient::audioFocusGained, m_audioEngine, &AudioEngine::onAudioFocusGained);
    connect(this, &ReflectorClient::activityPaused, m_audioEngine, &AudioEngine::onActivityPaused);
    connect(this, &ReflectorClient::activityResumed, m_audioEngine, &AudioEngine::onActivityResumed);
}

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
    QJsonObject nodeInfoJson;
    nodeInfoJson["sw"] = "Latry";
    nodeInfoJson["swVer"] = "latry-yo6say-0.0.14";
    nodeInfoJson["callsign"] = m_callsign;
#if defined(Q_OS_IOS)
    nodeInfoJson["tip"] = "I'm using <a href=\"https://latry.app\" target=\"_blank\">Latry.app</a>  by YO6SAY";
#else
    nodeInfoJson["tip"] = "I'm using <a href=\"https://latry.app\" target=\"_blank\">Latry.app</a> by YO6SAY";
#endif
    nodeInfoJson["Website"] = "https://latry.app";
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

void ReflectorClient::sendHeartbeat()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << (quint16)Svxlink::MsgType::HEARTBEAT;
    sendFrame(payload);
}

// --- UI and State Management ---
QString ReflectorClient::connectionStatus() const { return m_connectionStatus; }
bool ReflectorClient::pttActive() const { return m_pttActive; }
QString ReflectorClient::currentTalker() const { return m_currentTalker; }

void ReflectorClient::setMicGainDb(double gainDb) {
    // Clamp gain to -20dB to +20dB range for safety
    gainDb = qBound(-20.0, gainDb, 20.0);
    
    if (qAbs(m_micGainDb - gainDb) < 0.1) return; // Avoid unnecessary updates
    
    m_micGainDb = gainDb;
    emit micGainDbChanged();
    
    // Notify AudioEngine about the gain change if connected
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicGainDb", Qt::QueuedConnection, 
                                  Q_ARG(double, gainDb));
    }
    
    qDebug() << "Microphone gain set to:" << gainDb << "dB";
}
void ReflectorClient::connectToServer(const QString &host, int port, const QString &authKey, const QString &callsign, quint32 talkgroup) {
    if (m_state != Disconnected) return;
    
#if defined(Q_OS_ANDROID)
    startVoipService();
    saveConnectionState();
#endif

#if defined(Q_OS_IOS)
    // Start iOS VoIP service equivalent to Android VoipBackgroundService
    IOSVoIPHandler::instance()->startVoIPService(host, port, callsign, talkgroup);
    qDebug() << "iOS VoIP service started for connection";
#endif
    // Ensure fresh credentials - clear any cached auth data
    m_authKey.clear();
    
    m_host = host.trimmed(); m_port = port; m_authKey = authKey.trimmed().toUtf8(); m_callsign = callsign.trimmed(); m_talkgroup = talkgroup;
    m_clientId = 0;
    m_udpSequence = 0;
    m_state = Connecting; m_connectionStatus = "Connecting to " + m_host + "...";
    emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
    updateServiceConnectionStatus(m_connectionStatus, false);
#endif
    m_tcpBuffer.clear();
    m_lastAudioSeq = 0;
    m_nextUdpRxSeq = 0;
    m_currentTalker.clear();
    m_isReceivingAudio = false;
    m_audioTimeoutTimer->stop();

    // Configure TCP socket for freeze cycle survival
    m_tcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption, true);
    m_tcpSocket->setSocketOption(QAbstractSocket::LowDelayOption, true);

    // AudioEngine will handle audio restart automatically

    m_connectTimer->start(5000);
    m_tcpSocket->connectToHost(m_host, m_port);
}
void ReflectorClient::disconnectFromServer() {
#if defined(Q_OS_ANDROID)
    stopVoipService();
    clearConnectionState();
#endif

#if defined(Q_OS_IOS)
    // Stop iOS VoIP service equivalent to Android VoipBackgroundService
    IOSVoIPHandler::instance()->stopVoIPService();
    qDebug() << "iOS VoIP service stopped";
#endif
    m_heartbeatTimer->stop();
    if (m_txTimer) {
        m_txTimer->stop();
        m_txSeconds = 0;
        emit txTimeStringChanged();
    }
    // Cancel any pending connection attempt
    m_connectTimer->stop();
    // Explicitly cleanup audio resources when disconnecting
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "cleanup", Qt::QueuedConnection);
    }
    
    m_lastAudioSeq = 0;
    m_nextUdpRxSeq = 0;
    m_currentTalker.clear();
    m_isReceivingAudio = false;
    m_audioTimeoutTimer->stop();
    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }
    if (m_nameReply) {
        m_nameReply->abort();
        QMetaObject::invokeMethod(m_nameReply, "deleteLater", Qt::QueuedConnection);
        m_nameReply = nullptr;
    }
    
    // Clear cached authentication data to prevent stale credential reuse
    m_authKey.clear();
    
    m_clientId = 0;
    m_udpSequence = 0;
    if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        if (m_state == Connecting)
            m_tcpSocket->abort();
        else
            m_tcpSocket->disconnectFromHost();
    }
    if (m_state != Disconnected) {
        m_state = Disconnected;
        m_connectionStatus = "Disconnected";
        // Only reset audioReady when explicitly disconnecting (user action)
        // Don't reset it on automatic disconnects to preserve PTT functionality
        if (m_audioReady) {
            m_audioReady = false;
            emit audioReadyChanged();
        }
        emit connectionStatusChanged();
    }
}

// Notification disconnect method removed - keeping it simple

void ReflectorClient::pttPressed() {
    // Toggle PTT state
    if (m_pttActive) {
        pttReleased();
        return;
    }
    
    startTransmission();
}

void ReflectorClient::startTransmission() {
#if defined(Q_OS_ANDROID)
    const QString audioPerm = "android.permission.RECORD_AUDIO";
    auto permCheck = QtAndroidPrivate::checkPermission(audioPerm);
    if (permCheck.result() == QtAndroidPrivate::PermissionResult::Denied) {
        qDebug() << "PTT pressed but RECORD_AUDIO permission not granted, requesting permission";
        auto future = QtAndroidPrivate::requestPermission(audioPerm);
        auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            auto result = watcher->result();
            QMetaObject::invokeMethod(watcher, "deleteLater", Qt::QueuedConnection);
            if (result == QtAndroidPrivate::PermissionResult::Authorized) {
                qDebug() << "RECORD_AUDIO permission granted, proceeding with PTT";
                // Add a small delay to allow audio system to initialize after permission grant
                QTimer::singleShot(100, this, [this]() {
                    startTransmission();
                });
            } else {
                qWarning() << "RECORD_AUDIO permission denied by user";
            }
        });
        watcher->setFuture(future);
        return;
    }
#endif

#if defined(Q_OS_IOS)
    // iOS microphone permission is handled by the system via Info.plist
    // SECURITY: Configure audio session to DUCK other apps before PTT
    // This prevents other audio from mixing into microphone input
    ios_configureDuckingAudioSession();
    qDebug() << "iOS: Audio session configured for secure PTT (ducking mode)";
    
    // Request audio focus equivalent to Android audio focus
    IOSVoIPHandler::instance()->requestAudioFocus();
    qDebug() << "iOS audio focus requested for PTT";
#endif

    if (!m_audioReady) {
        qWarning() << "PTT pressed but audio not ready";
        return;
    }
    
    if (m_state != Connected || m_pttActive || !m_audioEngine) {
        return;
    }
    
    m_pttActive = true;
    emit pttActiveChanged();
    
    if (m_txTimer) {
        m_txSeconds = 0;
        emit txTimeStringChanged();
        m_txTimer->start();
    }
    
    // Start recording through AudioEngine
    QMetaObject::invokeMethod(m_audioEngine, "startRecording", Qt::QueuedConnection);
    qInfo() << "PTT Pressed: Recording started (secure mode - other apps ducked).";
}
void ReflectorClient::pttReleased() {
    if (!m_pttActive) return;
    m_pttActive = false; 
    emit pttActiveChanged();
    
    if (m_txTimer) {
        m_txTimer->stop();
        m_txSeconds = 0;
        emit txTimeStringChanged();
    }
    
#if defined(Q_OS_IOS)
    // Release audio focus equivalent to Android audio focus
    IOSVoIPHandler::instance()->abandonAudioFocus();
    qDebug() << "iOS audio focus released after PTT";
    
    // Send UDP heartbeat to re-register for RX audio after PTT
    // This ensures server knows we're ready to receive audio again
    QTimer::singleShot(100, this, [this]() {
        if (m_state == Connected && m_udpSocket && m_udpSocket->state() == QAbstractSocket::BoundState) {
            QByteArray datagram(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
            auto* udpBeat = reinterpret_cast<Svxlink::UdpMsgHeader*>(datagram.data());
            udpBeat->type = qToBigEndian((quint16)Svxlink::UdpMsgType::UDP_HEARTBEAT);
            udpBeat->clientId = qToBigEndian((quint16)m_clientId);
            udpBeat->sequenceNum = qToBigEndian(m_udpSequence++);
            sendUdpMessage(datagram);
            qDebug() << "iOS: Sent UDP heartbeat after PTT to re-register for RX audio";
        }
    });
#endif
    
    // Stop recording through AudioEngine
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "stopRecording", Qt::QueuedConnection);
    }
    
#if defined(Q_OS_IOS)
    // RESTORE: Configure audio session to MIX with other apps after PTT
    // This allows cooperative behavior with music, podcasts, etc.
    ios_configureMixingAudioSession();
    qDebug() << "iOS: Audio session restored to cooperative mixing mode after PTT";
#endif
    
    // Inform the server that we are done transmitting so talker state can be
    // updated immediately instead of waiting for a timeout.
    QByteArray flush(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
    auto* header = reinterpret_cast<Svxlink::UdpMsgHeader*>(flush.data());
    header->type = qToBigEndian((quint16)Svxlink::UdpMsgType::UDP_FLUSH_SAMPLES);
    header->clientId = qToBigEndian((quint16)m_clientId);
    header->sequenceNum = qToBigEndian(m_udpSequence++);
    sendUdpMessage(flush);
    
    qInfo() << "PTT Released: Recording stopped.";
}

// --- Network Slots ---
void ReflectorClient::onTcpConnected()
{
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
    m_connectTimer->stop();
    m_state = Disconnected; 
    m_connectionStatus = "Disconnected";
    
    qWarning() << "ReflectorClient::onTcpDisconnected - TCP connection lost"
               << "Previous state:" << (m_state == Connected ? "Connected" : m_state == Authenticating ? "Authenticating" : "Other")
               << "TCP socket error:" << m_tcpSocket->error()
               << "TCP socket error string:" << m_tcpSocket->errorString()
               << "Host was:" << m_host << ":" << m_port;
    
    // Don't reset audioReady on disconnect - AudioEngine maintains its state
    // This prevents PTT from being disabled after reconnection
    emit connectionStatusChanged();
    m_heartbeatTimer->stop();
    if (m_txTimer) {
        m_txTimer->stop();
        m_txSeconds = 0;
        emit txTimeStringChanged();
    }
    m_udpSocket->close();
    m_tcpBuffer.clear();
    m_currentTalker.clear();
    m_isReceivingAudio = false;
    m_audioTimeoutTimer->stop();
}

// --- Message Handling Helper Functions ---
void ReflectorClient::handleAuthChallenge(QDataStream &stream) {
    qDebug() << "Received AUTH_CHALLENGE from server.";
    quint16 len = 0;
    stream >> len;
    QByteArray challenge(len, 0);
    stream.readRawData(challenge.data(), len);
    QByteArray hmac = QMessageAuthenticationCode::hash(challenge, m_authKey, QCryptographicHash::Sha1);
    sendAuthResponse(hmac);
}

void ReflectorClient::handleServerInfo(QDataStream &stream) {
    quint16 reserved = 0;
    stream >> reserved >> m_clientId;
    m_state = Connected;
    m_connectionStatus = QString("Connected to TG %1").arg(m_talkgroup);
    
    qDebug() << "ReflectorClient::handleServerInfo - Authentication successful"
             << "Client ID:" << m_clientId
             << "Reserved field:" << reserved
             << "Target talkgroup:" << m_talkgroup
             << "TCP peer address for UDP:" << m_tcpSocket->peerAddress().toString();
    
    emit connectionStatusChanged();
#if defined(Q_OS_ANDROID)
    updateServiceConnectionStatus(m_connectionStatus, true);
#endif
    qInfo() << "Authenticated! ClientID:" << m_clientId;
    sendNodeInfo();
    sendSelectTG(m_talkgroup);
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
}

void ReflectorClient::handleTalkerStart(QDataStream &stream) {
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
        }
        if (!m_currentTalkerName.isEmpty()) {
            m_currentTalkerName.clear();
            emit currentTalkerNameChanged();
        }
        return;
    }
    
#if defined(Q_OS_IOS)
    // Request iOS audio focus when receiving audio from others
    // This ensures audio session is active after our PTT ended
    IOSVoIPHandler::instance()->requestAudioFocus();
    qDebug() << "iOS audio focus requested for incoming audio from:" << callsign;
#endif
    
    m_currentTalker = callsign;
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(m_currentTalker);
#endif
    m_currentTalkerName.clear();
    emit currentTalkerNameChanged();

    startNameLookup(callsign);
}

void ReflectorClient::handleTalkerStartV1(QDataStream &stream) {
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign == m_callsign) {
        if (!m_currentTalker.isEmpty()) {
            m_currentTalker.clear();
            emit currentTalkerChanged();
        }
        if (!m_currentTalkerName.isEmpty()) {
            m_currentTalkerName.clear();
            emit currentTalkerNameChanged();
        }
        return;
    }
    
#if defined(Q_OS_IOS)
    // Request iOS audio focus when receiving audio from others
    // This ensures audio session is active after our PTT ended
    IOSVoIPHandler::instance()->requestAudioFocus();
    qDebug() << "iOS audio focus requested for incoming audio from:" << callsign;
#endif
    
    m_currentTalker = callsign;
    emit currentTalkerChanged();
#if defined(Q_OS_ANDROID)
    updateServiceCurrentTalker(m_currentTalker);
#endif
    m_currentTalkerName.clear();
    emit currentTalkerNameChanged();

    startNameLookup(callsign);
}

void ReflectorClient::handleTalkerStop(QDataStream &stream) {
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
    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }
}

void ReflectorClient::handleTalkerStopV1(QDataStream &stream) {
    quint16 len = 0;
    stream >> len;
    QByteArray cs(len, 0);
    stream.readRawData(cs.data(), len);
    QString callsign = QString::fromUtf8(cs);
    if (callsign != m_currentTalker)
        return;
    m_currentTalker.clear();
    emit currentTalkerChanged();
    if (!m_currentTalkerName.isEmpty()) {
        m_currentTalkerName.clear();
        emit currentTalkerNameChanged();
    }
}

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

        qDebug() << "ReflectorClient::onTcpReadyRead - Processing TCP message type:" << messageType 
                 << "Payload size:" << payloadSize << "Connection state:" << m_state;
        
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
            // Handle protocol version downgrade request
            quint16 majorVer, minorVer;
            payloadStream >> majorVer >> minorVer;
            
            qWarning() << "Server requested protocol downgrade to" << majorVer << "." << minorVer;
            qWarning() << "Protocol downgrade not supported - disconnecting";
            
            m_connectionStatus = "Protocol version incompatible";
            emit connectionStatusChanged();
            m_state = Disconnected;
            break;
        }
        case Svxlink::MsgType::ERROR: {
            // Handle error message (replaces AUTH_DENIED)
            quint16 messageLen = 0;
            payloadStream >> messageLen;
            
            QByteArray errorMessage(messageLen, Qt::Uninitialized);
            payloadStream.readRawData(errorMessage.data(), messageLen);
            QString errorString = QString::fromLatin1(errorMessage);
            
            qWarning() << "Server error:" << errorString;
            m_connectionStatus = "Server error: " + errorString;
            emit connectionStatusChanged();
            
            // Clear cached auth key on authentication failures to prevent stale data reuse
            if (errorString.contains("Access denied", Qt::CaseInsensitive) || 
                errorString.contains("Authentication", Qt::CaseInsensitive)) {
                qDebug() << "Clearing cached auth key due to authentication failure";
                m_authKey.clear();
            }
            
            // Server will close connection, so prepare for disconnect
            m_state = Disconnected;
            break;
        }
        case Svxlink::MsgType::SERVER_INFO:
            qDebug() << "Received SERVER_INFO from server.";
            handleServerInfo(payloadStream);
            break;
        case Svxlink::MsgType::HEARTBEAT:
            qDebug() << "Received TCP Heartbeat from server.";
            break;
        case Svxlink::MsgType::NODE_LIST: {
            // Handle list of connected nodes
            quint16 nodeCount;
            payloadStream >> nodeCount;
            
            qDebug() << "Received NODE_LIST with" << nodeCount << "nodes";
            QStringList nodes;
            
            for (int i = 0; i < nodeCount; i++) {
                QByteArray callsignData(20, Qt::Uninitialized);
                payloadStream.readRawData(callsignData.data(), 20);
                QString callsign = QString::fromLatin1(callsignData).trimmed();
                if (!callsign.isEmpty()) {
                    nodes.append(callsign);
                }
            }
            
            emit connectedNodesChanged(nodes);
            qDebug() << "Connected nodes:" << nodes;
            break;
        }
        case Svxlink::MsgType::NODE_JOINED: {
            // Handle node joined notification
            QByteArray callsignData(20, Qt::Uninitialized);
            payloadStream.readRawData(callsignData.data(), 20);
            QString callsign = QString::fromLatin1(callsignData).trimmed();
            
            qDebug() << "Node joined:" << callsign;
            emit nodeJoined(callsign);
            break;
        }
        case Svxlink::MsgType::NODE_LEFT: {
            // Handle node left notification
            QByteArray callsignData(20, Qt::Uninitialized);
            payloadStream.readRawData(callsignData.data(), 20);
            QString callsign = QString::fromLatin1(callsignData).trimmed();
            
            qDebug() << "Node left:" << callsign;
            emit nodeLeft(callsign);
            break;
        }
        case Svxlink::MsgType::TG_MONITOR: {
            // Handle talk group monitor response
            quint16 tgCount;
            payloadStream >> tgCount;
            
            QList<quint32> talkgroups;
            for (int i = 0; i < tgCount; i++) {
                quint32 tg;
                payloadStream >> tg;
                talkgroups.append(tg);
            }
            
            qDebug() << "TG Monitor updated. Monitoring:" << talkgroups;
            emit monitoredTalkgroupsChanged(talkgroups);
            break;
        }
        case Svxlink::MsgType::REQUEST_QSY: {
            // Handle QSY request
            quint32 newTalkgroup;
            payloadStream >> newTalkgroup;
            
            qDebug() << "QSY requested to talkgroup:" << newTalkgroup;
            emit qsyRequested(newTalkgroup);
            break;
        }
        case Svxlink::MsgType::STATE_EVENT: {
            // Handle state event
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
            // Handle signal strength values
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
            // Handle TX status
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
            
            // Log payload hex dump for analysis
            if (payloadSize <= 64) {
                qDebug() << "Payload hex dump:" << payloadData.toHex(' ');
            }
            break;
        }
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
        
        switch (messageType) {
        case Svxlink::UdpMsgType::UDP_HEARTBEAT: {
            // Handle UDP heartbeat - just acknowledge receipt
            qDebug() << "Received UDP heartbeat from server";
            break;
        }
        case Svxlink::UdpMsgType::UDP_AUDIO: {
            const auto* msg = static_cast<const Svxlink::MsgUdpAudio*>(header);
            quint16 seq = qFromBigEndian(header->sequenceNum);
            int opusDataLen = qFromBigEndian(msg->audioLen);
            
            // Send raw Opus audio data to AudioEngine for processing
            if (m_audioEngine && opusDataLen > 0) {
                QByteArray audioData(reinterpret_cast<const char*>(msg->audioData), opusDataLen);
                QMetaObject::invokeMethod(m_audioEngine, "processReceivedAudio", Qt::QueuedConnection,
                                          Q_ARG(QByteArray, audioData), Q_ARG(quint16, seq));
                
                // Track that we're receiving audio (SVXLink-compatible approach)
                if (!m_isReceivingAudio) {
                    m_isReceivingAudio = true;
                    emit isReceivingAudioChanged();
                }
                // Reset timeout - restart the timer each time we receive audio
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
            // Clear receiving audio flag when transmission ends
            if (m_isReceivingAudio) {
                m_isReceivingAudio = false;
                m_audioTimeoutTimer->stop();
                emit isReceivingAudioChanged();
            }
            break;
        case Svxlink::UdpMsgType::UDP_ALL_SAMPLES_FLUSHED:
            qDebug() << "Received UDP all samples flushed";
            if (m_audioEngine) {
                QMetaObject::invokeMethod(m_audioEngine, "allSamplesFlushed", Qt::QueuedConnection);
            }
            break;
        case Svxlink::UdpMsgType::UDP_SIGNAL_STRENGTH: {
            // Handle UDP signal strength values
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
    
    qDebug() << "ReflectorClient::onAudioDataEncoded - Received" << encodedData.size() << "bytes, sending UDP packet";
    
    // Create UDP datagram with encoded audio data
    QByteArray datagram(sizeof(Svxlink::UdpMsgHeader) + sizeof(quint16) + encodedData.size(), Qt::Uninitialized);
    QDataStream ds(&datagram, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << (quint16)Svxlink::UdpMsgType::UDP_AUDIO
       << (quint16)m_clientId
       << (quint16)m_udpSequence++
       << (quint16)encodedData.size();
    ds.writeRawData(encodedData.constData(), encodedData.size());
    sendUdpMessage(datagram);
    
    qDebug() << "ReflectorClient::onAudioDataEncoded - UDP packet sent, sequence:" << (m_udpSequence-1);
}
void ReflectorClient::sendUdpMessage(const QByteArray &datagram)
{
    qDebug() << "ReflectorClient::sendUdpMessage - UDP socket state:" << m_udpSocket->state() 
             << "Expected BoundState:" << QAbstractSocket::BoundState;
    
    if (m_udpSocket->state() == QAbstractSocket::BoundState) {
        // Use the TCP connection's peer address (SVXLink pattern)
        // This eliminates asynchronous DNS lookup delays and ensures sequential packet transmission
        QHostAddress addr = m_tcpSocket->peerAddress();
        
        qDebug() << "ReflectorClient::sendUdpMessage - TCP socket state:" << m_tcpSocket->state()
                 << "TCP peer address:" << addr.toString()
                 << "Host:" << m_host << "Port:" << m_port;
        
        if (!addr.isNull()) {
            qint64 bytesWritten = m_udpSocket->writeDatagram(datagram, addr, m_port);
            qDebug() << "ReflectorClient::sendUdpMessage - UDP datagram sent successfully, bytes:" << bytesWritten
                     << "to" << addr.toString() << ":" << m_port;
        } else {
            qWarning() << "ReflectorClient::sendUdpMessage - No valid TCP peer address available"
                       << "TCP socket state:" << m_tcpSocket->state()
                       << "TCP socket peer:" << m_tcpSocket->peerName()
                       << "Host was:" << m_host;
        }
    } else {
        qWarning() << "ReflectorClient::sendUdpMessage - UDP socket not bound, state:" << m_udpSocket->state();
    }
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

QString ReflectorClient::currentTalkerName() const
{
    return m_currentTalkerName;
}

void ReflectorClient::onTxTimerTimeout()
{
    ++m_txSeconds;
    emit txTimeStringChanged();
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
    m_nameReply = m_networkManager->get(req);
    connect(m_nameReply, &QNetworkReply::finished, this, &ReflectorClient::onNameLookupFinished);
}

void ReflectorClient::onTcpError(QAbstractSocket::SocketError socketError)
{
    qWarning() << "ReflectorClient::onTcpError - TCP socket error occurred"
               << "Error code:" << socketError
               << "Error string:" << m_tcpSocket->errorString()
               << "Connection state:" << m_state
               << "Host:" << m_host << ":" << m_port;
    
    if (m_state != Disconnected) {
        m_connectTimer->stop();
        m_tcpSocket->abort();
        m_udpSocket->close();
        m_tcpBuffer.clear();
        m_currentTalker.clear();
        m_isReceivingAudio = false;
        m_audioTimeoutTimer->stop();
        m_state = Disconnected;
        m_connectionStatus = "Connection failed";
        // Don't reset audioReady on connection error - AudioEngine maintains its state
        // This prevents PTT from being disabled after reconnection
        emit connectionStatusChanged();
    }
}

void ReflectorClient::onConnectTimeout()
{
    if (m_state != Disconnected) {
        m_tcpSocket->abort();
        m_udpSocket->close();
        m_tcpBuffer.clear();
        m_currentTalker.clear();
        m_isReceivingAudio = false;
        m_audioTimeoutTimer->stop();
        m_state = Disconnected;
        m_connectionStatus = "Connection timeout";
        // Don't reset audioReady on connection timeout - AudioEngine maintains its state
        // This prevents PTT from being disabled after reconnection
        emit connectionStatusChanged();
    }
}

void ReflectorClient::onAudioSetupFinished()
{
    if (!m_audioReady) {
        m_audioReady = true;
        emit audioReadyChanged();
    }
}

void ReflectorClient::checkAndReconnect()
{
    qDebug() << "JNI: Connection check requested from Qt service";
    
    // Check socket state and connection health
    if (!m_tcpSocket) {
        qDebug() << "TCP socket is null, creating new connection...";
        if (!m_host.isEmpty() && m_port > 0) {
            connectToServer(m_host, m_port, QString::fromUtf8(m_authKey), m_callsign, m_talkgroup);
        }
        return;
    }
    
    QAbstractSocket::SocketState state = m_tcpSocket->state();
    qDebug() << "Current TCP socket state:" << state;
    
    // Handle various disconnected states
    if (state == QAbstractSocket::UnconnectedState || 
        state == QAbstractSocket::ClosingState || 
        state == QAbstractSocket::BoundState ||
        m_state == Disconnected) {
        
        qWarning() << "TCP disconnection detected during freeze/unfreeze cycle. State:" << state << "m_state:" << m_state;
        
        if (!m_host.isEmpty() && m_port > 0) {
            qInfo() << "Attempting immediate reconnection after freeze cycle";
            
            // Save current connection parameters
            QString savedHost = m_host;
            int savedPort = m_port;
            QByteArray savedAuthKey = m_authKey;
            QString savedCallsign = m_callsign;
            quint32 savedTalkgroup = m_talkgroup;
            
            // Force disconnect first to clean up
            if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
                m_tcpSocket->disconnectFromHost();
                if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
                    m_tcpSocket->waitForDisconnected(1000);
                }
            }
            
            // Reset state and reconnect immediately
            m_state = Disconnected;
            connectToServer(savedHost, savedPort, QString::fromUtf8(savedAuthKey), savedCallsign, savedTalkgroup);
        }
    } else if (state == QAbstractSocket::ConnectedState) {
        qDebug() << "TCP socket still connected - connection survived freeze cycle";
        
        // Test connection health by sending heartbeat
        if (m_state == Connected) {
            qDebug() << "Sending heartbeat to verify connection health";
            sendHeartbeat();
        }
    } else {
        qDebug() << "TCP socket in intermediate state:" << state << "- monitoring...";
    }
}

// iOS audio session callback implementations - always defined for MOC compatibility
void ReflectorClient::notifyIOSAudioSessionInterrupted()
{
#if defined(Q_OS_IOS)
    qDebug() << "iOS: Audio session interrupted - flushing audio buffers";
    
    // Flush audio buffers to prevent delayed/stale audio after interruption
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "flushAudioBuffers", Qt::QueuedConnection);
    }
    
    // Similar to Android audio focus lost
    emit audioFocusLost();
    
    // Release PTT if active during interruption
    if (m_pttActive) {
        pttReleased();
    }
#else
    // Stub for non-iOS platforms
    qDebug() << "notifyIOSAudioSessionInterrupted called on non-iOS platform";
#endif
}

void ReflectorClient::notifyIOSAudioSessionResumed()
{
#if defined(Q_OS_IOS)
    qDebug() << "iOS: Audio session resumed - restarting audio with delay";
    
    // Add a small delay before restarting audio to ensure iOS audio session is fully active
    QTimer::singleShot(200, this, [this]() {
        // Restart audio engine to clear any stale audio pipeline state
        if (m_audioEngine) {
            QMetaObject::invokeMethod(m_audioEngine, "restartAudio", Qt::QueuedConnection);
        }
        
        // Similar to Android audio focus gained
        IOSVoIPHandler::instance()->requestAudioFocus();
        emit audioFocusGained();
        
        qDebug() << "iOS: Audio restart completed after session resume";
    });
#else
    // Stub for non-iOS platforms
    qDebug() << "notifyIOSAudioSessionResumed called on non-iOS platform";
#endif
}

void ReflectorClient::notifyIOSBackgroundTaskExpired()
{
#if defined(Q_OS_IOS)
    qDebug() << "iOS: Background task expired";
    // Try to acquire a new background task if still connected
    if (m_state == Connected) {
        IOSVoIPHandler::instance()->acquireBackgroundTask();
    }
#else
    // Stub for non-iOS platforms  
    qDebug() << "notifyIOSBackgroundTaskExpired called on non-iOS platform";
#endif
}

#if defined(Q_OS_ANDROID)
void ReflectorClient::notifyAudioFocusLost()
{
    qDebug() << "JNI: Audio focus lost permanently";
    ReflectorClient* client = ReflectorClient::instance();
    if (client) {
        emit client->audioFocusLost();
    }
}

void ReflectorClient::notifyAudioFocusPaused()
{
    qDebug() << "JNI: Audio focus paused temporarily";
    ReflectorClient* client = ReflectorClient::instance();
    if (client) {
        emit client->audioFocusPaused();
    }
}

void ReflectorClient::notifyAudioFocusGained()
{
    qDebug() << "JNI: Audio focus gained";
    ReflectorClient* client = ReflectorClient::instance();
    if (client) {
        #if defined(Q_OS_ANDROID)
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        #endif
        emit client->audioFocusGained();
    }
}

void ReflectorClient::notifyActivityPaused()
{
    qDebug() << "JNI: Activity paused";
    ReflectorClient* client = ReflectorClient::instance();
    if (client) {
        emit client->activityPaused();
    }
}

void ReflectorClient::notifyActivityResumed()
{
    qDebug() << "JNI: Activity resumed";
    ReflectorClient* client = ReflectorClient::instance();
    if (client) {
        #if defined(Q_OS_ANDROID)
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        #endif
        emit client->activityResumed();
    }
}

// JNI callback implementations
extern "C" {
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusLost(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusLost();
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusPaused(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusPaused();
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyAudioFocusGained(JNIEnv *, jclass) {
        ReflectorClient::notifyAudioFocusGained();
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityPaused(JNIEnv *, jclass) {
        ReflectorClient::notifyActivityPaused();
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_LatryActivity_notifyActivityResumed(JNIEnv *, jclass) {
        ReflectorClient::notifyActivityResumed();
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStarted(JNIEnv *, jclass) {
        qDebug() << "JNI: VoIP service started";
        // Service started successfully - no specific action needed in C++
    }
    
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyServiceStopped(JNIEnv *, jclass) {
        qDebug() << "JNI: VoIP service stopped";
        // Service stopped - no specific action needed in C++
    }
    
    // Notification disconnect functionality removed - keeping it simple
    
    JNIEXPORT void JNICALL Java_yo6say_latry_VoipBackgroundService_notifyCheckConnection(JNIEnv *, jclass) {
        qDebug() << "JNI: Connection check requested from Qt service";
        ReflectorClient* client = ReflectorClient::instance();
        if (client) {
            // Check if we're still connected, and if not, attempt reconnection
            QMetaObject::invokeMethod(client, "checkAndReconnect", Qt::QueuedConnection);
        }
    }
    
    // Mute functionality removed
}

void ReflectorClient::acquireWakeLock()
{
    qDebug() << "Acquiring wake lock for background VoIP";
    QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "acquireWakeLock", "()V");
}

void ReflectorClient::releaseWakeLock()
{
    qDebug() << "Releasing wake lock";
    QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "releaseWakeLock", "()V");
}

void ReflectorClient::startVoipService()
{
    qDebug() << "Starting VoIP background service";
    QJniObject context = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");
    QJniObject hostStr = QJniObject::fromString(m_host);
    QJniObject callsignStr = QJniObject::fromString(m_callsign);
    
    // Use Qt-compliant static method for service startup (per Qt documentation)
    QJniObject::callStaticMethod<void>("yo6say/latry/VoipBackgroundService", 
        "startQtVoipService", 
        "(Landroid/content/Context;)V",
        context.object());
    
    // Also call the parameter version for backward compatibility
    QJniObject::callStaticMethod<void>("yo6say/latry/VoipBackgroundService", 
        "startVoipService", 
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;I)V",
        context.object(),
        hostStr.object(),
        m_port,
        callsignStr.object(),
        m_talkgroup
    );
}

void ReflectorClient::stopVoipService()
{
    qDebug() << "Stopping VoIP background service";
    QJniObject context = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");
    
    QJniObject::callStaticMethod<void>("yo6say/latry/VoipBackgroundService", 
        "stopVoipService", 
        "(Landroid/content/Context;)V",
        context.object()
    );
}

void ReflectorClient::updateServiceConnectionStatus(const QString& status, bool connected)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        QJniObject statusStr = QJniObject::fromString(status);
        serviceInstance.callMethod<void>("updateConnectionStatus", "(Ljava/lang/String;Z)V", 
            statusStr.object(), connected);
    }
}

void ReflectorClient::updateServiceCurrentTalker(const QString& talker)
{
    QJniObject serviceInstance = QJniObject::callStaticObjectMethod("yo6say/latry/VoipBackgroundService", "getInstance", "()Lyo6say/latry/VoipBackgroundService;");
    if (serviceInstance.isValid()) {
        QJniObject talkerStr = QJniObject::fromString(talker);
        serviceInstance.callMethod<void>("updateCurrentTalker", "(Ljava/lang/String;)V", 
            talkerStr.object());
    }
}

// Mute functionality removed

void ReflectorClient::saveConnectionState()
{
    QJniObject context = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");
    QJniObject hostStr = QJniObject::fromString(m_host);
    QJniObject callsignStr = QJniObject::fromString(m_callsign);
    
    QJniObject::callStaticMethod<void>("yo6say/latry/BootReceiver", 
        "saveConnectionState", 
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;I)V",
        context.object(),
        hostStr.object(),
        m_port,
        callsignStr.object(),
        m_talkgroup
    );
}

void ReflectorClient::clearConnectionState()
{
    QJniObject context = QJniObject::callStaticObjectMethod("org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");
    
    QJniObject::callStaticMethod<void>("yo6say/latry/BootReceiver", 
        "clearConnectionState", 
        "(Landroid/content/Context;)V",
        context.object()
    );
}

// Mute functionality removed
#endif


