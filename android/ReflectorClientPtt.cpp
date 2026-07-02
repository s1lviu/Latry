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
#include <QMetaObject>
#include <QTimer>
#include <QtEndian>
#include <QDebug>

void ReflectorClient::togglePtt()
{
    if (m_pttPermissionRestartPending) {
        m_pttPermissionRestartPending = false;
        qInfo() << "PTT toggle cancelled pending microphone permission retry.";
        return;
    }

    if (m_pttReleasePending) {
        forcePttRelease();
        return;
    }

    if (m_pttActive) {
        pttReleased();
        return;
    }

    pttPressed();
}

void ReflectorClient::pttPressed()
{
    if (m_pttReleasePending) {
        cancelPendingPttRelease();
        qInfo() << "PTT press cancelled pending hangtime release.";
        return;
    }

    if (m_pttPermissionRestartPending) {
        qDebug() << "PTT press ignored while RECORD_AUDIO permission request is pending";
        return;
    }

    if (m_pttActive || m_txStopPending) {
        return;
    }

    startTransmission();
}

void ReflectorClient::startTransmission()
{
#if defined(Q_OS_ANDROID)
    if (!hasAuthorizedRecordAudioPermission()) {
        qDebug() << "PTT pressed but RECORD_AUDIO permission not granted, requesting permission";
        m_pttPermissionRestartPending = true;
        requestRecordAudioPermissionIfNeeded();
        return;
    }
#endif

    if (!m_audioReady) {
        qWarning() << "PTT pressed but audio not ready";
        return;
    }

    if (m_state != Connected || m_pttActive || m_txStopPending || !m_audioEngine) {
        return;
    }

    if (m_talkgroup == 0) {
        if (m_defaultTalkgroup == 0) {
            qWarning() << "PTT pressed while parked in TG 0 but no default talkgroup is configured";
            return;
        }
        selectTalkgroupInternal(m_defaultTalkgroup, TalkgroupSelectionOrigin::TxDefaultActivation);
    } else {
        m_usePriorityMode = false;
        resetTalkgroupSelectionTimer();
    }

#if defined(Q_OS_ANDROID)
    QMetaObject::invokeMethod(m_audioEngine, "setupAudioInput", Qt::BlockingQueuedConnection);
#endif

    m_pttActive = true;
    emit pttActiveChanged();

#if defined(Q_OS_ANDROID)
    bool recordingStarted = false;
    QMetaObject::invokeMethod(
        m_audioEngine,
        [this, &recordingStarted]() {
            m_audioEngine->startRecording();
            recordingStarted = m_audioEngine->isRecording();
        },
        Qt::BlockingQueuedConnection);

    if (!recordingStarted) {
        m_pttActive = false;
        emit pttActiveChanged();
        qWarning() << "PTT pressed but Android TX capture did not start";
        return;
    }
#else
    // Start recording through AudioEngine
    QMetaObject::invokeMethod(m_audioEngine, "startRecording", Qt::QueuedConnection);
#endif

#if defined(Q_OS_ANDROID)
    updateServiceTransmitState(true);
#endif

    m_txTimeoutWarningFeedbackSent = false;
    if (m_txTimer) {
        m_txSeconds = 0;
        emit txTimeStringChanged();
        m_txTimer->start();
    }

    updateTxTimeoutWarningState();

    qInfo() << "PTT Pressed: Recording started.";
}

void ReflectorClient::pttReleased()
{
#if defined(Q_OS_ANDROID)
    m_resumeAndroidPttAfterReconnect = false;
#endif

    if (m_pttPermissionRestartPending && !m_pttActive) {
        m_pttPermissionRestartPending = false;
        qInfo() << "PTT release cancelled pending microphone permission retry.";
        return;
    }

    if (!m_pttActive || m_txStopPending || m_pttReleasePending) {
        return;
    }

    if (m_pttHangTimeMs <= 0) {
        beginImmediatePttRelease();
        return;
    }

    m_pttReleasePending = true;
    updateTxTimeoutWarningState();
    if (m_pttHangTimer) {
        m_pttHangTimer->start(m_pttHangTimeMs);
    }

    qInfo() << "PTT Released: Delaying TX stop by" << m_pttHangTimeMs
            << "ms before drain and flush.";
}

void ReflectorClient::forcePttRelease()
{
#if defined(Q_OS_ANDROID)
    m_resumeAndroidPttAfterReconnect = false;
#endif

    m_pttPermissionRestartPending = false;

    if (!m_pttActive && !m_pttReleasePending) {
        return;
    }

    cancelPendingPttRelease();
    beginImmediatePttRelease();
}

void ReflectorClient::cancelPendingPttRelease()
{
    if (m_pttHangTimer && m_pttHangTimer->isActive()) {
        m_pttHangTimer->stop();
    }

    if (!m_pttReleasePending) {
        return;
    }

    m_pttReleasePending = false;
    updateTxTimeoutWarningState();
}

void ReflectorClient::beginImmediatePttRelease()
{
    if (!m_pttActive || m_txStopPending) {
        return;
    }

    m_txStopPending = true;
    updateTxTimeoutWarningState();

    // Stop recording through AudioEngine
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "stopRecording", Qt::QueuedConnection);
    } else {
        onTxDrainComplete();
    }

    qInfo() << "PTT Released: Draining remaining TX audio before flush.";
}

void ReflectorClient::onPttHangTimerTimeout()
{
    if (!m_pttReleasePending || !m_pttActive) {
        cancelPendingPttRelease();
        return;
    }

    m_pttReleasePending = false;
    beginImmediatePttRelease();
}

void ReflectorClient::onTxDrainComplete()
{
    if (!m_txStopPending) {
        return;
    }

    m_txStopPending = false;
    sendTxFlushSamples();

    if (m_pttActive) {
        m_pttActive = false;
        emit pttActiveChanged();
    }

#if defined(Q_OS_ANDROID)
    updateServiceTransmitState(false);
#endif

    m_txTimeoutWarningFeedbackSent = false;
    if (m_txTimer) {
        m_txTimer->stop();
        m_txSeconds = 0;
        emit txTimeStringChanged();
    }

    updateTxTimeoutWarningState();

    qInfo() << "PTT Released: Recording stopped after TX drain.";
}

void ReflectorClient::stopTransmissionCaptureForDisconnect()
{
    if (!m_audioEngine) {
        return;
    }

    QMetaObject::invokeMethod(m_audioEngine, "stopRecording", Qt::QueuedConnection);
}

#if defined(Q_OS_ANDROID)
void ReflectorClient::resumeAndroidPttAfterReconnectIfReady()
{
    if (!m_resumeAndroidPttAfterReconnect
            || m_state != Connected
            || !m_audioReady
            || m_pttActive
            || m_pttReleasePending
            || m_txStopPending) {
        return;
    }

    m_resumeAndroidPttAfterReconnect = false;
    QTimer::singleShot(0, this, [this]() {
        if (m_state == Connected
                && m_audioReady
                && !m_pttActive
                && !m_pttReleasePending
                && !m_txStopPending) {
            pttPressed();
        }
    });
}
#endif

void ReflectorClient::sendTxFlushSamples()
{
    QByteArray flush(sizeof(Svxlink::UdpMsgHeader), Qt::Uninitialized);
    auto* header = reinterpret_cast<Svxlink::UdpMsgHeader*>(flush.data());
    header->type = qToBigEndian((quint16)Svxlink::UdpMsgType::UDP_FLUSH_SAMPLES);
    header->clientId = qToBigEndian((quint16)m_clientId);
    header->sequenceNum = qToBigEndian(m_udpSequence++);
    sendUdpMessage(flush);
}
