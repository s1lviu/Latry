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
#include "AudioEngine.h"
#include <memory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
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

    Q_INVOKABLE void connectToServer(const QString &host, int port, const QString &authKey, const QString &callsign, quint32 talkgroup);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void pttPressed();
    Q_INVOKABLE void pttReleased();

#if defined(Q_OS_ANDROID)
    // Android audio focus callbacks (public for JNI access)
    static void notifyAudioFocusLost();
    static void notifyAudioFocusPaused();
    static void notifyAudioFocusGained();
    static void notifyActivityPaused();
    static void notifyActivityResumed();
#endif

private:
    void startTransmission();


signals:
    void connectionStatusChanged();
    void pttActiveChanged();
    void currentTalkerChanged();
    void currentTalkerNameChanged();
    void txTimeStringChanged();
    void audioReadyChanged();
    void isReceivingAudioChanged();
    
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
    void checkAndReconnect();

private:
    ~ReflectorClient();
    ReflectorClient(const ReflectorClient&) = delete;
    ReflectorClient& operator=(const ReflectorClient&) = delete;

    void sendFrame(const QByteArray &payload);
    void sendProtoVer();
    void sendAuthResponse(const QByteArray &hmac);
    void sendNodeInfo();
    void sendSelectTG(quint32 talkgroup);
    void sendHeartbeat();
    void sendUdpMessage(const QByteArray &datagram);
    void setupAudio();
    void initializeAudioEngine();
    
#if defined(Q_OS_ANDROID)
    void acquireWakeLock();
    void releaseWakeLock();
    void startVoipService();
    void stopVoipService();
    void updateServiceConnectionStatus(const QString& status, bool connected);
    void updateServiceCurrentTalker(const QString& talker);
    // Mute functionality removed
    void saveConnectionState();
    void clearConnectionState();
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
    quint16 m_clientId = 0;
    uint16_t m_udpSequence = 0;

    // Audio engine and thread
    AudioEngine* m_audioEngine = nullptr;
    QThread* m_audioThread = nullptr;

    QString m_currentTalker;
    quint16 m_lastAudioSeq = 0;
    quint16 m_nextUdpRxSeq = 0;
    bool m_isReceivingAudio = false;
    QTimer* m_audioTimeoutTimer = nullptr;

    QTimer* m_txTimer = nullptr;
    QTimer* m_connectTimer = nullptr;
    int m_txSeconds = 0;

    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_nameReply = nullptr;
    QString m_currentTalkerName;
    QDateTime m_lastTalkerTimestamp;
};

#endif // REFLECTORCLIENT_H
