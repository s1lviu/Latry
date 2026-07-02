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
#include "AndroidAudioRecordInput.h"
#include <QDebug>
#include <QTimer>
#include <QMetaObject>
#include <algorithm>
#include <opus.h>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniObject>
#endif

namespace {
constexpr int kTxStartupLeadInFrames = 2;
}

void AudioEngine::startRecording()
{
    qDebug() << "AudioEngine::startRecording called - audioSource:" << (m_audioSource ? "OK" : "NULL")
             << "androidInput:" << (m_androidAudioRecordInput ? "OK" : "NULL")
             << "recording:" << m_recording << "audioReady:" << m_audioReady;

    if (!m_audioSource && !m_androidAudioRecordInput) {
        qWarning() << "AudioEngine::startRecording - No audio input path available, trying to set one up";
        setupAudioInput();
        if (!m_audioSource && !m_androidAudioRecordInput) {
            qWarning() << "AudioEngine::startRecording - Audio input still not available after setup, retrying in 200ms";
            // Retry after a short delay to allow audio system to initialize
            QTimer::singleShot(200, this, [this]() {
                setupAudioInput();
                if (m_audioSource || m_androidAudioRecordInput) {
                    qDebug() << "AudioEngine::startRecording - Audio input available after retry, starting recording";
                    startRecording();
                } else {
                    qWarning() << "AudioEngine::startRecording - Audio input still not available after retry";
                }
            });
            return;
        }
    }

    if (m_recording) {
        qDebug() << "AudioEngine::startRecording - Already recording";
        return;
    }

#if defined(Q_OS_ANDROID)
    if (m_androidAudioRecordInput) {
        // Set MODE_IN_COMMUNICATION and request exclusive focus for TX
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "setAudioMode", "(I)V", 3);
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocusForTX", "()V");

        m_pendingInputSamples.clear();
        prepareTxStartupPriming();
        m_recording = true;

        if (startAndroidCaptureInput()) {
            m_audioInputDevice = nullptr;
            sendTxStartupLeadIn();
            qDebug() << "AudioEngine::startRecording - Android native capture started (TX mode)";
            return;
        }

        m_recording = false;
        resetTxStartupPriming();
        m_pendingInputSamples.clear();

        // Restore RX mode on failure
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "setAudioMode", "(I)V", 0);
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocusForRX", "()V");

        qWarning() << "AudioEngine::startRecording - Android native capture failed; no Qt audio fallback is allowed";
        return;
    }
#endif

    if (!m_audioSource) {
        qWarning() << "AudioEngine::startRecording - No Qt audio source available";
        return;
    }

    m_audioInputDevice = m_audioSource->start();
    if (m_audioInputDevice) {
        connect(m_audioInputDevice, &QIODevice::readyRead, this, &AudioEngine::onAudioInputReadyRead);
        m_recording = true;
        qDebug() << "AudioEngine::startRecording - Recording started successfully";
    } else {
        qWarning() << "AudioEngine::startRecording - Failed to start audio input device, state:" << m_audioSource->state();
    }
}

void AudioEngine::stopRecording()
{
    if (!m_recording) {
        resetTxStartupPriming();
        emit txDrainComplete();
        return;
    }

#if defined(Q_OS_ANDROID)
    if (usesAndroidNativeInput()) {
        stopAndroidCaptureInput();
        m_recording = false;
        flushPendingTxSamples();
        m_pendingInputSamples.clear();
        resetTxStartupPriming();
        m_audioInputDevice = nullptr;

        // Restore RX mode: MODE_NORMAL + RX mixing focus
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "setAudioMode", "(I)V", 0);
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocusForRX", "()V");

        qDebug() << "Recording stopped (Android native input, restored RX mode)";
        emit txDrainComplete();
        return;
    }
#endif

    if (!m_audioSource) {
        m_recording = false;
        flushPendingTxSamples();
        m_pendingInputSamples.clear();
        resetTxStartupPriming();
        emit txDrainComplete();
        return;
    }

    // Disconnect signal immediately to prevent further callbacks during cleanup
    if (m_audioInputDevice) {
        disconnect(m_audioInputDevice, &QIODevice::readyRead, this, &AudioEngine::onAudioInputReadyRead);
    }

    // Reset audio mode when stopping recording
    resetAudioMode();

#if defined(Q_OS_ANDROID)
    // On Android with OpenSL ES, the most reliable way to avoid mutex timeouts
    // is to recreate the audio source for each recording session
    if (m_audioSource) {
        QMetaObject::invokeMethod(m_audioSource, "deleteLater", Qt::QueuedConnection);
        m_audioSource = nullptr;
        m_audioInputDevice = nullptr;
        m_pendingInputSamples.clear();
        resetTxStartupPriming();
        qDebug() << "Recording stopped (Android - source deleted)";
    }
#else
    // On other platforms, use normal stop
    if (m_audioSource && m_audioSource->state() != QAudio::StoppedState) {
        m_audioSource->stop();
    }
    m_audioInputDevice = nullptr;
    qDebug() << "Recording stopped";
#endif

    m_recording = false;
    flushPendingTxSamples();
    m_pendingInputSamples.clear();
    resetTxStartupPriming();
    emit txDrainComplete();
}

void AudioEngine::onAudioInputReadyRead()
{
    if (!m_recording || !m_audioInputDevice || !m_encoder || !m_audioSource) {
        qDebug() << "AudioEngine::onAudioInputReadyRead - Not ready:"
                 << "recording:" << m_recording
                 << "inputDevice:" << (m_audioInputDevice ? "OK" : "NULL")
                 << "encoder:" << (m_encoder ? "OK" : "NULL")
                 << "audioSource:" << (m_audioSource ? "OK" : "NULL");
        return;
    }

    QByteArray pcmData = m_audioInputDevice->readAll();
    if (pcmData.isEmpty()) {
        qDebug() << "AudioEngine::onAudioInputReadyRead - No data available";
        return;
    }

    int samplesRead = 0;
    const int inputChannels = m_inputFormat.channelCount();
    qDebug() << "AudioEngine::onAudioInputReadyRead - Processing" << pcmData.size() << "bytes of audio data, channels:" << inputChannels;

    if (m_inputFormat.sampleFormat() == QAudioFormat::Int16) {
        const qint16* src = reinterpret_cast<const qint16*>(pcmData.constData());
        const int totalSamples = pcmData.size() / sizeof(qint16);
        const int monoSamples = totalSamples / inputChannels;

        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }

        if (inputChannels == 1) {
            for (int i = 0; i < monoSamples; ++i) {
                m_reusableFloatBuffer[i] = src[i] * (1.0f / 32768.0f);
            }
        } else {
            const float invChannels = 1.0f / inputChannels;
            const float invScale = invChannels / 32768.0f;
            for (int i = 0; i < monoSamples; ++i) {
                float sum = 0.0f;
                for (int ch = 0; ch < inputChannels; ++ch) {
                    sum += src[i * inputChannels + ch];
                }
                m_reusableFloatBuffer[i] = sum * invScale;
            }
        }
        samplesRead = monoSamples;
    } else if (m_inputFormat.sampleFormat() == QAudioFormat::Float) {
        const float* src = reinterpret_cast<const float*>(pcmData.constData());
        const int totalSamples = pcmData.size() / sizeof(float);
        const int monoSamples = totalSamples / inputChannels;

        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }

        if (inputChannels == 1) {
            std::copy(src, src + monoSamples, m_reusableFloatBuffer.data());
        } else {
            const float invChannels = 1.0f / inputChannels;
            for (int i = 0; i < monoSamples; ++i) {
                float sum = 0.0f;
                for (int ch = 0; ch < inputChannels; ++ch) {
                    sum += src[i * inputChannels + ch];
                }
                m_reusableFloatBuffer[i] = sum * invChannels;
            }
        }
        samplesRead = monoSamples;
    } else if (m_inputFormat.sampleFormat() == QAudioFormat::Int32) {
        const qint32* src = reinterpret_cast<const qint32*>(pcmData.constData());
        const int totalSamples = pcmData.size() / sizeof(qint32);
        const int monoSamples = totalSamples / inputChannels;

        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }

        if (inputChannels == 1) {
            for (int i = 0; i < monoSamples; ++i) {
                m_reusableFloatBuffer[i] = src[i] * (1.0f / 2147483648.0f);
            }
        } else {
            const float invChannels = 1.0f / inputChannels;
            const float invScale = invChannels / 2147483648.0f;
            for (int i = 0; i < monoSamples; ++i) {
                float sum = 0.0f;
                for (int ch = 0; ch < inputChannels; ++ch) {
                    sum += src[i * inputChannels + ch];
                }
                m_reusableFloatBuffer[i] = sum * invScale;
            }
        }
        samplesRead = monoSamples;
    }

    // Apply resampling if needed
    const float* sampleData = m_reusableFloatBuffer.data();
    std::vector<float> resampledData;
    if (m_inputResampler) {
        resampledData = m_inputResampler->process(sampleData, samplesRead);
        sampleData = resampledData.data();
        samplesRead = resampledData.size();
    }

    processCapturedFloatSamples(const_cast<float*>(sampleData), samplesRead);
}

bool AudioEngine::startAndroidCaptureInput()
{
#if defined(Q_OS_ANDROID)
    if (!m_androidAudioRecordInput) {
        m_androidAudioRecordInput = std::make_unique<AndroidAudioRecordInput>(
            [this](const short* samples, int count, int sampleRate) {
                queueCapturedInt16Samples(samples, count, sampleRate);
            },
            [this](float* samples, int count, int sampleRate) {
                queueCapturedNativeFloatSamples(samples, count, sampleRate);
            });
    }

    if (!m_androidAudioRecordInput->start()) {
        return false;
    }

    const int captureSampleRate = m_androidAudioRecordInput->sampleRate();
    m_inputFormat.setSampleRate(captureSampleRate);
    m_inputFormat.setChannelCount(CHANNELS);
    m_inputFormat.setSampleFormat(
        m_androidAudioRecordInput->usesFloatCapture() ? QAudioFormat::Float : QAudioFormat::Int16);

    if (captureSampleRate != SAMPLE_RATE) {
        m_inputResampler = std::make_unique<Resampler>(captureSampleRate, SAMPLE_RATE, CHANNELS);
    } else {
        m_inputResampler.reset();
    }

    return true;
#else
    return false;
#endif
}

void AudioEngine::stopAndroidCaptureInput()
{
#if defined(Q_OS_ANDROID)
    if (m_androidAudioRecordInput) {
        m_androidAudioRecordInput->stop();
    }
#endif
}

void AudioEngine::releaseAndroidCaptureInput()
{
#if defined(Q_OS_ANDROID)
    if (m_androidAudioRecordInput) {
        m_androidAudioRecordInput->release();
    }
#endif
}

bool AudioEngine::usesAndroidNativeInput() const
{
#if defined(Q_OS_ANDROID)
    return m_androidAudioRecordInput && m_androidAudioRecordInput->isCapturing();
#else
    return false;
#endif
}

void AudioEngine::queueCapturedNativeFloatSamples(const float* samples, int count, int sampleRate)
{
    if (samples == nullptr || count <= 0) {
        return;
    }

    std::vector<float> sampleCopy(samples, samples + count);
    QMetaObject::invokeMethod(
        this,
        [this, sampleCopy = std::move(sampleCopy), sampleRate]() mutable {
            processCapturedNativeFloatSamples(sampleCopy.data(),
                                             static_cast<int>(sampleCopy.size()),
                                             sampleRate);
        },
        Qt::QueuedConnection);
}

void AudioEngine::queueCapturedInt16Samples(const short* samples, int count, int sampleRate)
{
    if (samples == nullptr || count <= 0) {
        return;
    }

    // Marshal native capture back onto the AudioEngine thread so TX state
    // and the Opus encoder are never mutated concurrently from the JNI thread.
    std::vector<short> sampleCopy(samples, samples + count);
    QMetaObject::invokeMethod(
        this,
        [this, sampleCopy = std::move(sampleCopy), sampleRate]() mutable {
            processCapturedInt16Samples(sampleCopy.data(),
                                       static_cast<int>(sampleCopy.size()),
                                       sampleRate);
        },
        Qt::QueuedConnection);
}

int AudioEngine::encodeTxFrame(const float* frameSamples)
{
    if (!m_encoder || frameSamples == nullptr) {
        return OPUS_BAD_ARG;
    }

    const int encodedBytes = m_encoder->encode(
        frameSamples,
        FRAME_SIZE_SAMPLES,
        m_reusableOpusBuffer.data(),
        OPUS_BUFFER_SIZE);
    if (encodedBytes <= 0) {
        return encodedBytes;
    }

    QByteArray encodedData(reinterpret_cast<const char*>(m_reusableOpusBuffer.data()), encodedBytes);
    emit audioDataEncoded(encodedData);
    return encodedBytes;
}

void AudioEngine::encodeReadyTxFrames(const char* logContext)
{
    while (m_pendingInputSamples.size() >= FRAME_SIZE_SAMPLES) {
        const int encodedBytes = encodeTxFrame(m_pendingInputSamples.data());
        if (encodedBytes > 0) {
            if (logContext != nullptr) {
                qDebug() << logContext << encodedBytes
                         << "bytes, emitted audioDataEncoded signal";
            }
        } else {
            qWarning() << "Opus encode error:" << opus_strerror(encodedBytes);
            break;
        }

        m_pendingInputSamples.erase(
            m_pendingInputSamples.begin(),
            m_pendingInputSamples.begin() + FRAME_SIZE_SAMPLES);
    }
}

void AudioEngine::prepareTxStartupPriming()
{
    resetTxStartupPriming();
    m_txStartupPrimingActive = true;
    m_txStartupPrimingTargetSamples = kTxStartupLeadInFrames * FRAME_SIZE_SAMPLES;
    m_txStartupBuffer.reserve(static_cast<size_t>(m_txStartupPrimingTargetSamples) * 2U);
}

void AudioEngine::sendTxStartupLeadIn()
{
    if (!m_txStartupPrimingActive) {
        return;
    }

    int sentFrames = 0;
    for (int i = 0; i < kTxStartupLeadInFrames; ++i) {
        const int encodedBytes = encodeTxFrame(m_txLeadInSilenceFrame.data());
        if (encodedBytes <= 0) {
            qWarning() << "Opus encode error while sending TX lead-in silence:"
                       << opus_strerror(encodedBytes);
            break;
        }
        ++sentFrames;
    }

    if (sentFrames > 0) {
        qDebug() << "AudioEngine::sendTxStartupLeadIn - Sent" << sentFrames
                 << "TX lead-in silence frames before releasing buffered speech";
    }
}

void AudioEngine::flushBufferedTxStartupAudio()
{
    if (m_txStartupBuffer.empty()) {
        resetTxStartupPriming();
        return;
    }

    m_pendingInputSamples.reserve(m_pendingInputSamples.size() + m_txStartupBuffer.size());
    m_pendingInputSamples.insert(
        m_pendingInputSamples.end(),
        m_txStartupBuffer.begin(),
        m_txStartupBuffer.end());

    qDebug() << "AudioEngine::flushBufferedTxStartupAudio - Released"
             << m_txStartupBuffer.size() << "startup TX samples after priming";

    resetTxStartupPriming();
}

void AudioEngine::resetTxStartupPriming()
{
    m_txStartupPrimingActive = false;
    m_txStartupPrimingTargetSamples = 0;
    m_txStartupBuffer.clear();
}

void AudioEngine::flushPendingTxSamples()
{
    if (!m_encoder) {
        resetTxStartupPriming();
        return;
    }

    if (m_txStartupPrimingActive) {
        flushBufferedTxStartupAudio();
    }

    if (m_pendingInputSamples.empty()) {
        return;
    }

    if (m_pendingInputSamples.size() < FRAME_SIZE_SAMPLES) {
        m_pendingInputSamples.resize(FRAME_SIZE_SAMPLES, 0.0f);
    }

    while (m_pendingInputSamples.size() >= FRAME_SIZE_SAMPLES) {
        const int encodedBytes = encodeTxFrame(m_pendingInputSamples.data());
        if (encodedBytes > 0) {
            qDebug() << "AudioEngine::flushPendingTxSamples - Encoded final" << encodedBytes
                     << "byte TX frame during drain";
        } else {
            qWarning() << "Opus encode error during TX drain:" << opus_strerror(encodedBytes);
            break;
        }

        m_pendingInputSamples.erase(
            m_pendingInputSamples.begin(),
            m_pendingInputSamples.begin() + FRAME_SIZE_SAMPLES);
    }
}

void AudioEngine::processCapturedFloatSamples(float* samples, int count)
{
    if (!m_recording || !m_encoder || samples == nullptr || count <= 0) {
        return;
    }

    applyTxGain(samples, count);
    m_audioLimiter.processAudio(samples, count);
    updateTxMeter(samples, count);

    if (m_txStartupPrimingActive) {
        m_txStartupBuffer.insert(m_txStartupBuffer.end(), samples, samples + count);
        if (static_cast<int>(m_txStartupBuffer.size()) < m_txStartupPrimingTargetSamples) {
            return;
        }

        flushBufferedTxStartupAudio();
    } else {
        m_pendingInputSamples.reserve(m_pendingInputSamples.size() + count);
        m_pendingInputSamples.insert(m_pendingInputSamples.end(), samples, samples + count);
    }

    encodeReadyTxFrames(nullptr);
}

void AudioEngine::processCapturedNativeFloatSamples(float* samples, int count, int sampleRate)
{
    if (!m_recording || samples == nullptr || count <= 0) {
        return;
    }

    float* sampleData = samples;
    int samplesRead = count;
    std::vector<float> resampledData;

    if (sampleRate != SAMPLE_RATE) {
        if (!m_inputResampler
                || m_inputFormat.sampleRate() != sampleRate
                || m_inputFormat.sampleFormat() != QAudioFormat::Float) {
            m_inputResampler = std::make_unique<Resampler>(sampleRate, SAMPLE_RATE, CHANNELS);
            m_inputFormat.setSampleRate(sampleRate);
            m_inputFormat.setChannelCount(CHANNELS);
            m_inputFormat.setSampleFormat(QAudioFormat::Float);
        }

        resampledData = m_inputResampler->process(sampleData, samplesRead);
        sampleData = resampledData.data();
        samplesRead = static_cast<int>(resampledData.size());
    }

    processCapturedFloatSamples(sampleData, samplesRead);
}

void AudioEngine::processCapturedInt16Samples(const short* samples, int count, int sampleRate)
{
    if (!m_recording || samples == nullptr || count <= 0) {
        return;
    }

    if (m_reusableFloatBuffer.size() < static_cast<size_t>(count)) {
        m_reusableFloatBuffer.resize(static_cast<size_t>(count));
    }

    constexpr float kInt16Scale = 1.0f / 32768.0f;
    for (int i = 0; i < count; ++i) {
        m_reusableFloatBuffer[static_cast<size_t>(i)] = samples[i] * kInt16Scale;
    }

    float* sampleData = m_reusableFloatBuffer.data();
    int samplesRead = count;
    std::vector<float> resampledData;

    if (sampleRate != SAMPLE_RATE) {
        if (!m_inputResampler || m_inputFormat.sampleRate() != sampleRate) {
            m_inputResampler = std::make_unique<Resampler>(sampleRate, SAMPLE_RATE, CHANNELS);
            m_inputFormat.setSampleRate(sampleRate);
            m_inputFormat.setChannelCount(CHANNELS);
            m_inputFormat.setSampleFormat(QAudioFormat::Int16);
        }

        resampledData = m_inputResampler->process(sampleData, samplesRead);
        sampleData = resampledData.data();
        samplesRead = static_cast<int>(resampledData.size());
    }

    processCapturedFloatSamples(sampleData, samplesRead);
}
