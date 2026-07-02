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

#include "AudioEngine.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniObject>
#endif

void AudioEngine::restartAudio()
{
    qDebug() << "Restarting audio due to focus recovery";

    if (m_meterDecayTimer && !m_meterDecayTimer->isActive()) {
        m_meterDecayTimer->start();
    }

#if defined(Q_OS_ANDROID)
    if (usesAndroidNativeOutput()) {
        resumeAndroidPlaybackOutput();

        // Update last audio write time
        m_lastAudioWrite = QDateTime::currentDateTime();

        // Clear focus flags
        m_audioFocusLost = false;
        m_audioFocusPaused = false;

        if (m_inputResampler) {
            m_inputResampler->reset();
        }

        m_pendingInputSamples.clear();
        resetTxStartupPriming();

        if (!m_audioReady) {
            m_audioReady = true;
            emit audioReadyChanged(true);
        }

        qDebug() << "Android audio restart completed - AudioReady:" << m_audioReady;
        return;
    }
#endif

    // Stop current audio
    if (m_audioSink) {
        m_audioSink->stop();
    }

    // Wait a bit for cleanup then restart
    QTimer::singleShot(100, this, [this]() {
        if (m_audioSink && m_audioStreamDevice) {
            m_audioSink->start(m_audioStreamDevice);
        }

        // Update last audio write time
        m_lastAudioWrite = QDateTime::currentDateTime();

        // Clear focus flags
        m_audioFocusLost = false;
        m_audioFocusPaused = false;

        // Reset input resampler to clear stale buffered samples
        if (m_inputResampler) {
            m_inputResampler->reset();
        }

        // Clear pending input samples to avoid audio artifacts
        m_pendingInputSamples.clear();
        resetTxStartupPriming();

        // Ensure audioReady is set after successful restart
        if (!m_audioReady) {
            m_audioReady = true;
            emit audioReadyChanged(true);
        }

        qDebug() << "Audio restart completed - AudioReady:" << m_audioReady;
    });
}

void AudioEngine::checkAudioHealth()
{
    // Check if audio sink is in a good state
    if (!usesAndroidNativeOutput() && m_audioSink) {
        if (m_audioSink->state() == QAudio::SuspendedState ||
            m_audioSink->state() == QAudio::StoppedState) {
            qDebug() << "Audio sink is suspended/stopped, requesting restart";
            restartAudio();
        }
    }

    // Check if we haven't received audio in a while (more than 5 seconds)
    if (m_lastAudioWrite.isValid() &&
        m_lastAudioWrite.secsTo(QDateTime::currentDateTime()) > 5) {
        // Flush jitter buffer (SVXLink equivalent of flushEncodedSamples)
        m_jitterBuffer.clear();

        // Reset audio sequence tracking
        m_lastAudioSeq = 0;
        m_hasLastAudioSeq = false;
        m_lastDecodedFrameSamples = FRAME_SIZE_SAMPLES;

        // Clear the timestamp to prevent repeated flushing
        m_lastAudioWrite = QDateTime();

        // Re-request audio focus if needed
        #if defined(Q_OS_ANDROID)
        if (!m_audioFocusLost && !m_audioFocusPaused) {
            QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        }
        #endif
    }
}

void AudioEngine::onAudioRecoveryTimer()
{
    if (!m_audioReady || m_audioFocusLost) {
        return;
    }

    checkAudioHealth();
}

void AudioEngine::onAudioFocusLost()
{
    qDebug() << "AudioEngine::onAudioFocusLost - Audio focus lost permanently";
    m_audioFocusLost = true;
    m_audioFocusPaused = false;

    // Stop audio recovery timer
    if (m_audioRecoveryTimer) {
        m_audioRecoveryTimer->stop();
    }

    // Stop audio output safely
    if (usesAndroidNativeOutput()) {
        stopAndroidPlaybackOutput();
    } else if (m_audioSink) {
        try {
            if (m_audioSink->state() != QAudio::StoppedState) {
                m_audioSink->stop();
            }
        } catch (...) {
            qWarning() << "Exception during audioSink stop on focus lost";
        }
    }

    // Set audio as not ready
    if (m_audioReady) {
        m_audioReady = false;
        emit audioReadyChanged(false);
    }

    emit audioRecoveryNeeded();
}

void AudioEngine::onAudioFocusPaused()
{
    qDebug() << "AudioEngine::onAudioFocusPaused - Audio focus paused temporarily";
    m_audioFocusPaused = true;

    // Pause audio output safely
    if (usesAndroidNativeOutput()) {
        pauseAndroidPlaybackOutput();
    } else if (m_audioSink) {
        try {
            if (m_audioSink->state() == QAudio::ActiveState) {
                m_audioSink->suspend();
            }
        } catch (...) {
            qWarning() << "Exception during audioSink suspend on focus paused";
        }
    }
}

void AudioEngine::onAudioFocusGained()
{
    qDebug() << "Audio focus gained";

    // Only restart if we were paused, not if we lost focus permanently
    if (m_audioFocusPaused && !m_audioFocusLost) {
        restartAudio();
    }

    // Start audio recovery timer
    if (m_audioRecoveryTimer && !m_audioRecoveryTimer->isActive()) {
        m_audioRecoveryTimer->start();
    }
}

void AudioEngine::onActivityPaused()
{
    qDebug() << "AudioEngine::onActivityPaused - Activity paused, keeping audio alive for background VoIP";

    // UI/background transitions must not tear down the live audio path.
    // Real audio interruptions are handled through the audio focus callbacks.
    if (m_audioRecoveryTimer && !m_audioRecoveryTimer->isActive()) {
        m_audioRecoveryTimer->start();
    }
}

void AudioEngine::onActivityResumed()
{
    qDebug() << "AudioEngine::onActivityResumed - Activity resumed";

    const bool wasPaused = m_audioFocusPaused;
    const bool wasLost = m_audioFocusLost;

#if defined(Q_OS_ANDROID)
    if (wasPaused || wasLost) {
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity",
                                           "requestAudioFocus",
                                           "()V");
    }
#endif

    if (wasPaused) {
        m_audioFocusLost = false;
        m_audioFocusPaused = false;
        QTimer::singleShot(100, this, &AudioEngine::restartAudio);
    } else if (wasLost && !m_audioReady) {
        m_audioFocusLost = false;
        m_audioFocusPaused = false;
        qDebug() << "AudioEngine::onActivityResumed - Rebuilding audio after focus loss";
        QTimer::singleShot(100, this, &AudioEngine::setupAudio);
    }

    // Start audio recovery timer
    if (m_audioRecoveryTimer && !m_audioRecoveryTimer->isActive()) {
        m_audioRecoveryTimer->start();
    }
}

void AudioEngine::handleAudioRouteChanged()
{
#if defined(Q_OS_ANDROID)
    const bool hadAndroidOutput = usesAndroidNativeOutput();
    const bool hadAndroidInput = usesAndroidNativeInput();
    const bool hadQtOutput = (m_audioSink != nullptr);
    const bool hadOutput = hadAndroidOutput || hadQtOutput;
    const bool hadInput = hadAndroidInput || (m_audioSource != nullptr);
    const bool wasRecording = m_recording;

    if (!hadOutput && !hadInput) {
        qDebug() << "AudioEngine: Ignoring Android audio route change because no audio devices are active";
        return;
    }

    qDebug() << "AudioEngine: Rebuilding Qt audio devices for Android route change"
             << "hadOutput=" << hadOutput
             << "hadAndroidOutput=" << hadAndroidOutput
             << "hadAndroidInput=" << hadAndroidInput
             << "hadQtOutput=" << hadQtOutput
             << "hadInput=" << hadInput
             << "wasRecording=" << wasRecording;

    if (wasRecording) {
        stopRecording();
    }

    bool needsOutputSetup = hadQtOutput;
    if (hadAndroidOutput) {
        if (!applyCurrentAndroidPlaybackRoute()) {
            qWarning() << "AudioEngine: Failed to apply Android native playback route, restarting output";
            stopAndroidPlaybackOutput();
            if (!startAndroidPlaybackOutput()) {
                qWarning() << "AudioEngine: Failed to restart Android native playback after route change";
                needsOutputSetup = true;
            }
        }
    }

    if (hadQtOutput && m_audioSink) {
        try {
            if (m_audioSink->state() != QAudio::StoppedState) {
                m_audioSink->stop();
            }
        } catch (...) {
            qWarning() << "AudioEngine: Exception while stopping audio sink for route change";
        }
        QMetaObject::invokeMethod(m_audioSink, "deleteLater", Qt::QueuedConnection);
        m_audioSink = nullptr;
    }

    if (hadQtOutput && m_audioStreamDevice) {
        QMetaObject::invokeMethod(m_audioStreamDevice, "deleteLater", Qt::QueuedConnection);
        m_audioStreamDevice = nullptr;
    }

    if (m_audioSource) {
        try {
            if (m_audioSource->state() != QAudio::StoppedState) {
                m_audioSource->stop();
            }
        } catch (...) {
            qWarning() << "AudioEngine: Exception while stopping audio source for route change";
        }
        QMetaObject::invokeMethod(m_audioSource, "deleteLater", Qt::QueuedConnection);
        m_audioSource = nullptr;
        m_audioInputDevice = nullptr;
    }

    if (hadQtOutput) {
        m_outputResampler.reset();
        m_outputFormat = QAudioFormat();
    }
    m_inputResampler.reset();
    m_pendingInputSamples.clear();
    resetTxStartupPriming();

    if (hadQtOutput && m_audioReady) {
        m_audioReady = false;
        emit audioReadyChanged(false);
    }

    if (needsOutputSetup) {
        if (m_audioReady) {
            m_audioReady = false;
            emit audioReadyChanged(false);
        }
        setupAudio();
    }

    if (wasRecording) {
        startRecording();
    } else if (hadInput) {
        setupAudioInput();
    }
#endif
}

void AudioEngine::configureAudioForVoIP()
{
#if defined(Q_OS_ANDROID)
    try {
        // Set MODE_NORMAL for RX/idle state
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "setAudioMode", "(I)V", 0);

        // Request RX mixing focus
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocusForRX", "()V");

        // Reapply audio route
        QJniObject context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative", "getContext", "()Landroid/content/Context;");
        if (context.isValid()) {
            QJniObject::callStaticMethod<void>(
                "yo6say/latry/LatryAudioRouteManager",
                "reapplyPreferredRoute",
                "(Landroid/content/Context;)V",
                context.object());
        }
    } catch (...) {
        qWarning() << "AudioEngine: Exception during VoIP audio configuration";
    }
#endif

    qDebug() << "AudioEngine: VoIP audio configuration completed (RX mode)";
}

void AudioEngine::resetAudioMode()
{
#if defined(Q_OS_ANDROID)
    try {
        // Set MODE_NORMAL
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "setAudioMode", "(I)V", 0);

        // Clear communication device on Android 12+
        int sdkVersion = QJniObject::getStaticField<jint>(
            "android/os/Build$VERSION", "SDK_INT");
        if (sdkVersion >= 31) {
            QJniObject context = QJniObject::callStaticObjectMethod(
                "org/qtproject/qt/android/QtNative", "getContext", "()Landroid/content/Context;");
            if (context.isValid()) {
                QJniObject audioManager = context.callObjectMethod(
                    "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    QJniObject::fromString("audio").object());
                if (audioManager.isValid()) {
                    audioManager.callMethod<void>("clearCommunicationDevice");
                }
            }
        }
        qDebug() << "AudioEngine: Reset to MODE_NORMAL";
    } catch (...) {
        qWarning() << "AudioEngine: Exception during audio mode reset";
    }
#endif
}
