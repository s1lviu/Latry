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

#ifndef IOSVOIPHANDLER_H
#define IOSVOIPHANDLER_H

#include <QObject>
#include <QString>

#ifdef Q_OS_IOS
#include "IOSAudioManager.h"
#endif

// iOS VoIP background handler - equivalent to Android VoipBackgroundService
class IOSVoIPHandler : public QObject
{
    Q_OBJECT

public:
    static IOSVoIPHandler* instance();
    explicit IOSVoIPHandler(QObject *parent = nullptr);
    ~IOSVoIPHandler();
    
    // Make static instance accessible for C callbacks
    static IOSVoIPHandler* getStaticInstance() { return s_instance; }

    // Public interface - mirrors Android VoipBackgroundService functionality
    Q_INVOKABLE void startVoIPService(const QString &host, int port, const QString &callsign, int talkgroup);
    Q_INVOKABLE void stopVoIPService();
    Q_INVOKABLE bool isServiceRunning() const;
    
    // Connection status updates (called by ReflectorClient)
    void updateConnectionStatus(const QString &status, bool connected);
    void updateCurrentTalker(const QString &talker);

public slots:
    // Audio session management
    void requestAudioFocus();
    void abandonAudioFocus();
    
    // Background task management
    void acquireBackgroundTask();
    void releaseBackgroundTask();
    
    // Screen wake lock management (equivalent to Android wake lock)
    void acquireScreenWakeLock();
    void releaseScreenWakeLock();

signals:
    // Signals to notify ReflectorClient about system events
    void audioSessionInterrupted();
    void audioSessionResumed();
    void backgroundTaskExpired();
    void serviceStarted();
    void serviceStopped();

public slots:
    // Public slot for C callback access
    void handleAudioInterruption(int type);

private:
    void initializeService();
    void cleanupService();
    
    // Service state
    bool m_serviceRunning;
    bool m_audioSessionActive;
    bool m_backgroundTaskActive;
    bool m_screenWakeLockActive;
    
    // Connection parameters
    QString m_serverHost;
    int m_serverPort;
    QString m_callsign;
    int m_talkGroup;
    
    // Connection status
    QString m_connectionStatus;
    QString m_currentTalker;
    bool m_isConnected;
    
    static IOSVoIPHandler* s_instance;
};

// C++ interface for Objective-C callbacks
#ifdef __cplusplus
extern "C" {
#endif

void handleIOSAudioInterruption(int type);

#ifdef __cplusplus
}
#endif

#endif // IOSVOIPHANDLER_H