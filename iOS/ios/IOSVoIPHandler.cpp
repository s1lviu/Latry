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

#include "IOSVoIPHandler.h"
#include <QDebug>
#include <QTimer>

#ifdef Q_OS_IOS
#include "IOSAudioManager.h"
#endif

IOSVoIPHandler* IOSVoIPHandler::s_instance = nullptr;

IOSVoIPHandler* IOSVoIPHandler::instance()
{
    if (!s_instance) {
        s_instance = new IOSVoIPHandler();
    }
    return s_instance;
}

IOSVoIPHandler::IOSVoIPHandler(QObject *parent)
    : QObject(parent)
    , m_serviceRunning(false)
    , m_audioSessionActive(false)
    , m_backgroundTaskActive(false)
    , m_screenWakeLockActive(false)
    , m_serverPort(0)
    , m_talkGroup(0)
    , m_isConnected(false)
{
    qDebug() << "IOSVoIPHandler: Initializing iOS VoIP handler";
    initializeService();
}

IOSVoIPHandler::~IOSVoIPHandler()
{
    cleanupService();
    s_instance = nullptr;
}

void IOSVoIPHandler::initializeService()
{
#ifdef Q_OS_IOS
    // Configure iOS audio session for VoIP
    ios_configureVoIPAudioSession();
    qDebug() << "IOSVoIPHandler: iOS audio session configured for VoIP";
#endif
}

void IOSVoIPHandler::cleanupService()
{
    if (m_serviceRunning) {
        stopVoIPService();
    }
    
    // Release screen wake lock on cleanup
    releaseScreenWakeLock();
}

void IOSVoIPHandler::startVoIPService(const QString &host, int port, const QString &callsign, int talkgroup)
{
    qDebug() << "IOSVoIPHandler: Starting VoIP service for" << host << ":" << port;
    
    if (m_serviceRunning) {
        qWarning() << "IOSVoIPHandler: Service already running, stopping first";
        stopVoIPService();
    }
    
    // Store connection parameters
    m_serverHost = host;
    m_serverPort = port;
    m_callsign = callsign;
    m_talkGroup = talkgroup;
    m_connectionStatus = QString("Connecting to %1:%2").arg(host).arg(port);
    m_isConnected = false;
    
    // Configure and activate audio session - this keeps iOS VoIP apps running indefinitely
    // No background task needed - active audio session is sufficient for VoIP background mode
    requestAudioFocus();
    
    // Acquire screen wake lock when VoIP service starts (matches Android behavior)
    // This prevents screen timeout while the app is connected, not just during PTT
    acquireScreenWakeLock();
    
    m_serviceRunning = true;
    
    qDebug() << "IOSVoIPHandler: VoIP service started successfully";
    emit serviceStarted();
}

void IOSVoIPHandler::stopVoIPService()
{
    qDebug() << "IOSVoIPHandler: Stopping VoIP service";
    
    if (!m_serviceRunning) {
        qDebug() << "IOSVoIPHandler: Service not running, nothing to stop";
        return;
    }
    
    m_serviceRunning = false;
    m_isConnected = false;
    m_connectionStatus = "Disconnected";
    m_currentTalker.clear();
    
    // Release audio session only on explicit disconnect (like Android stopping foreground service)
    abandonAudioFocus();
    
    // Release screen wake lock when service stops
    releaseScreenWakeLock();
    
    qDebug() << "IOSVoIPHandler: VoIP service stopped";
    emit serviceStopped();
}

bool IOSVoIPHandler::isServiceRunning() const
{
    return m_serviceRunning;
}

void IOSVoIPHandler::updateConnectionStatus(const QString &status, bool connected)
{
    m_connectionStatus = status;
    m_isConnected = connected;
    
    qDebug() << "IOSVoIPHandler: Connection status updated:" << status << "Connected:" << connected;
    
    // iOS VoIP background mode: Audio session keeps app running, no background tasks needed
    // Connection state changes don't require background task management
}

void IOSVoIPHandler::updateCurrentTalker(const QString &talker)
{
    m_currentTalker = talker;
    qDebug() << "IOSVoIPHandler: Current talker updated:" << talker;
}

void IOSVoIPHandler::requestAudioFocus()
{
    if (m_audioSessionActive) {
        qDebug() << "IOSVoIPHandler: Audio session already active";
        return;
    }
    
#ifdef Q_OS_IOS
    // Activate iOS audio session - equivalent to Android AudioManager.requestAudioFocus()
    ios_activateAudioSession();
    m_audioSessionActive = true;
    qDebug() << "IOSVoIPHandler: Audio focus acquired";
#else
    qDebug() << "IOSVoIPHandler: Audio focus request (non-iOS platform)";
#endif
}

void IOSVoIPHandler::abandonAudioFocus()
{
    if (!m_audioSessionActive) {
        qDebug() << "IOSVoIPHandler: Audio session already inactive";
        return;
    }
    
#ifdef Q_OS_IOS
    // Deactivate iOS audio session - equivalent to Android AudioManager.abandonAudioFocus()
    ios_deactivateAudioSession();
    m_audioSessionActive = false;
    qDebug() << "IOSVoIPHandler: Audio focus abandoned";
#else
    qDebug() << "IOSVoIPHandler: Audio focus abandoned (non-iOS platform)";
#endif
}

void IOSVoIPHandler::acquireBackgroundTask()
{
    if (m_backgroundTaskActive) {
        qDebug() << "IOSVoIPHandler: Background task already active";
        return;
    }
    
#ifdef Q_OS_IOS
    // Start iOS background task - equivalent to Android wake lock
    ios_beginBackgroundTask();
    m_backgroundTaskActive = ios_isBackgroundTaskActive();
    
    if (m_backgroundTaskActive) {
        qDebug() << "IOSVoIPHandler: Background task acquired for VoIP";
    } else {
        qWarning() << "IOSVoIPHandler: Failed to acquire background task";
    }
#else
    qDebug() << "IOSVoIPHandler: Background task acquired (non-iOS platform)";
    m_backgroundTaskActive = true;
#endif
}

void IOSVoIPHandler::releaseBackgroundTask()
{
    if (!m_backgroundTaskActive) {
        qDebug() << "IOSVoIPHandler: Background task already inactive";
        return;
    }
    
#ifdef Q_OS_IOS
    // End iOS background task - equivalent to Android wake lock release
    ios_endBackgroundTask();
    m_backgroundTaskActive = false;
    qDebug() << "IOSVoIPHandler: Background task released";
#else
    qDebug() << "IOSVoIPHandler: Background task released (non-iOS platform)";
    m_backgroundTaskActive = false;
#endif
}

void IOSVoIPHandler::acquireScreenWakeLock()
{
    if (m_screenWakeLockActive) {
        qDebug() << "IOSVoIPHandler: Screen wake lock already active";
        return;
    }
    
#ifdef Q_OS_IOS
    // Acquire iOS screen wake lock - equivalent to Android PowerManager.WakeLock
    ios_acquireScreenWakeLock();
    m_screenWakeLockActive = ios_isScreenWakeLockActive();
    
    if (m_screenWakeLockActive) {
        qDebug() << "IOSVoIPHandler: Screen wake lock acquired (prevents screen timeout)";
    } else {
        qWarning() << "IOSVoIPHandler: Failed to acquire screen wake lock";
    }
#else
    qDebug() << "IOSVoIPHandler: Screen wake lock acquired (non-iOS platform)";
    m_screenWakeLockActive = true;
#endif
}

void IOSVoIPHandler::releaseScreenWakeLock()
{
    if (!m_screenWakeLockActive) {
        qDebug() << "IOSVoIPHandler: Screen wake lock already inactive";
        return;
    }
    
#ifdef Q_OS_IOS
    // Release iOS screen wake lock - equivalent to Android wake lock release
    ios_releaseScreenWakeLock();
    m_screenWakeLockActive = false;
    qDebug() << "IOSVoIPHandler: Screen wake lock released (screen timeout restored)";
#else
    qDebug() << "IOSVoIPHandler: Screen wake lock released (non-iOS platform)";
    m_screenWakeLockActive = false;
#endif
}

void IOSVoIPHandler::handleAudioInterruption(int type)
{
    qDebug() << "IOSVoIPHandler: Audio interruption received, type:" << type;
    
    switch (type) {
        case 0: // Interruption began
            qDebug() << "IOSVoIPHandler: Audio session interrupted";
            m_audioSessionActive = false;
            emit audioSessionInterrupted();
            break;
            
        case 1: // Interruption ended, should resume
            qDebug() << "IOSVoIPHandler: Audio session interruption ended, resuming";
            requestAudioFocus();
            emit audioSessionResumed();
            break;
            
        case 2: // Interruption ended, should not resume
            qDebug() << "IOSVoIPHandler: Audio session interruption ended, not resuming";
            m_audioSessionActive = false;
            emit audioSessionInterrupted();
            break;
            
        default:
            qWarning() << "IOSVoIPHandler: Unknown audio interruption type:" << type;
            break;
    }
}

// C interface for Objective-C callbacks
extern "C" {

void handleIOSAudioInterruption(int type)
{
    // Forward audio interruption to the iOS VoIP handler
    IOSVoIPHandler* instance = IOSVoIPHandler::getStaticInstance();
    if (instance) {
        instance->handleAudioInterruption(type);
    } else {
        qWarning() << "handleIOSAudioInterruption: No IOSVoIPHandler instance available";
    }
}

}