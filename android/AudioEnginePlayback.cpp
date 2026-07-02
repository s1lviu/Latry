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
#include "AndroidAudioTrackOutput.h"
#include <algorithm>
#include <QDebug>
#include <QDateTime>
#include <opus.h>

#if defined(Q_OS_ANDROID)
#  include <cerrno>
#  include <fcntl.h>
#  include <unistd.h>
#endif

bool AudioEngine::startAndroidPlaybackOutput()
{
#if defined(Q_OS_ANDROID)
    if (!m_androidAudioTrackOutput) {
        m_androidAudioTrackOutput = std::make_unique<AndroidAudioTrackOutput>(&m_jitterBuffer);
    }

    return m_androidAudioTrackOutput->start();
#else
    return false;
#endif
}

void AudioEngine::stopAndroidPlaybackOutput()
{
#if defined(Q_OS_ANDROID)
    if (m_androidAudioTrackOutput) {
        m_androidAudioTrackOutput->stop();
    }
#endif
}

void AudioEngine::pauseAndroidPlaybackOutput()
{
#if defined(Q_OS_ANDROID)
    if (m_androidAudioTrackOutput) {
        m_androidAudioTrackOutput->pause();
    }
#endif
}

void AudioEngine::resumeAndroidPlaybackOutput()
{
#if defined(Q_OS_ANDROID)
    if (m_androidAudioTrackOutput) {
        m_androidAudioTrackOutput->resume();
    }
#endif
}

bool AudioEngine::applyCurrentAndroidPlaybackRoute()
{
#if defined(Q_OS_ANDROID)
    if (!m_androidAudioTrackOutput) {
        return false;
    }

    return m_androidAudioTrackOutput->applyCurrentRoute();
#else
    return false;
#endif
}

bool AudioEngine::usesAndroidNativeOutput() const
{
#if defined(Q_OS_ANDROID)
    return m_androidAudioTrackOutput && m_androidAudioTrackOutput->isActive();
#else
    return false;
#endif
}

void AudioEngine::setTranscriptionPipeFd(int fd)
{
#if defined(Q_OS_ANDROID)
    if (m_transcriptionPipeFd == fd) {
        return;
    }

    if (m_transcriptionPipeFd >= 0) {
        ::close(m_transcriptionPipeFd);
        m_transcriptionPipeFd = -1;
    }

    if (fd < 0) {
        return;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        qWarning() << "AudioEngine: Failed to configure transcription pipe as non-blocking";
        ::close(fd);
        return;
    }

    m_transcriptionPipeFd = fd;
#else
    Q_UNUSED(fd)
#endif
}

void AudioEngine::processReceivedAudio(const QByteArray &audioData, quint16 sequence)
{
    if (!m_decoder || !m_audioReady) {
        qDebug() << "AudioEngine::processReceivedAudio - Audio not ready, skipping"
                 << "decoder:" << (m_decoder ? "OK" : "NULL")
                 << "audioReady:" << m_audioReady;
        return;
    }

    // Sequence number gap handling with bounded PLC
    if (m_hasLastAudioSeq) {
        const quint16 expected = static_cast<quint16>(m_lastAudioSeq + 1);
        const quint16 diff = static_cast<quint16>(sequence - expected);

        if (diff > 0x7fff) {
            // Out-of-order / stale packet — drop (SvxLink convention)
            return;
        }

        if (diff > 0) {
            // Lost frames — apply PLC for up to 3 frames (60ms), skip the rest
            constexpr unsigned kMaxPlcFrames = 3;
            const unsigned plcCount = std::min(static_cast<unsigned>(diff), kMaxPlcFrames);
            const int plcFrameSamples = std::clamp(m_lastDecodedFrameSamples,
                                                   FRAME_SIZE_SAMPLES,
                                                   MAX_FRAME_SIZE_SAMPLES);
            for (unsigned i = 0; i < plcCount; ++i) {
                std::vector<float> plc(static_cast<size_t>(plcFrameSamples * CHANNELS));
                int plcSamples = m_decoder->decode(nullptr, 0, plc.data(), plcFrameSamples);
                if (plcSamples > 0) {
                    applyRxGain(plc.data(), plcSamples);
                    updateRxMeter(plc.data(), plcSamples);
                    m_jitterBuffer.writeSamples(plc.data(), plcSamples);
                }
            }
            if (diff > kMaxPlcFrames) {
                qDebug() << "AudioEngine: skipped" << (diff - kMaxPlcFrames)
                         << "lost frames beyond PLC limit";
            }
        }
    }

    // Decode the Opus audio data - hybrid approach for optimal performance
    // Try normal 20ms buffer first, fallback to larger buffer for v1 clients with 40/60ms frames
    std::vector<float> decodedSamples(FRAME_SIZE_SAMPLES * CHANNELS);
    int decodedSampleCount = m_decoder->decode(
        reinterpret_cast<const unsigned char*>(audioData.constData()),
        audioData.size(),
        decodedSamples.data(),
        FRAME_SIZE_SAMPLES
    );

    // If buffer too small, retry with larger buffer for legacy clients
    if (decodedSampleCount == OPUS_BUFFER_TOO_SMALL) {
        decodedSamples.resize(MAX_FRAME_SIZE_SAMPLES * CHANNELS);
        decodedSampleCount = m_decoder->decode(
            reinterpret_cast<const unsigned char*>(audioData.constData()),
            audioData.size(),
            decodedSamples.data(),
            MAX_FRAME_SIZE_SAMPLES
        );
    }

    if (decodedSampleCount > 0) {
        m_lastDecodedFrameSamples = decodedSampleCount;
        applyRxGain(decodedSamples.data(), decodedSampleCount);
        updateRxMeter(decodedSamples.data(), decodedSampleCount);

#if defined(Q_OS_ANDROID)
        if (m_transcriptionPipeFd >= 0) {
            m_transcriptionPcmBuffer.resize(static_cast<size_t>(decodedSampleCount));
            for (int i = 0; i < decodedSampleCount; ++i) {
                const float sample = clampAudioSample(decodedSamples[i]);
                m_transcriptionPcmBuffer[static_cast<size_t>(i)] =
                        static_cast<int16_t>(sample * 32767.0f);
            }

            const size_t bytesToWrite =
                    m_transcriptionPcmBuffer.size() * sizeof(int16_t);
            const ssize_t written = ::write(m_transcriptionPipeFd,
                                            m_transcriptionPcmBuffer.data(),
                                            bytesToWrite);
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Drop this frame instead of blocking the RX audio thread.
                } else if (errno == EPIPE || errno == EBADF) {
                    qWarning() << "AudioEngine: transcription pipe closed unexpectedly";
                    ::close(m_transcriptionPipeFd);
                    m_transcriptionPipeFd = -1;
                } else {
                    qWarning() << "AudioEngine: transcription pipe write failed with errno"
                               << errno;
                }
            }
        }
#endif

        // Write the NATIVE 16kHz samples directly to the jitter buffer
        // DO NOT RESAMPLE HERE - AudioStreamDevice will handle resampling
        m_jitterBuffer.writeSamples(decodedSamples.data(), decodedSampleCount);

        // Trigger the AudioStreamDevice to notify QAudioSink that data is available
        if (m_audioStreamDevice) {
            m_audioStreamDevice->triggerReadyRead();
        }

        // Update last audio write time
        m_lastAudioWrite = QDateTime::currentDateTime();
    } else {
        qWarning() << "Opus decode error:" << opus_strerror(decodedSampleCount);
    }

    m_lastAudioSeq = sequence;
    m_hasLastAudioSeq = true;
}

void AudioEngine::flushAudioBuffers()
{
    qDebug() << "AudioEngine::flushAudioBuffers - Starting flush";

    // Clear jitter buffer
    m_jitterBuffer.clear();

    // Reset last audio sequence
    m_lastAudioSeq = 0;
    m_hasLastAudioSeq = false;
    m_lastDecodedFrameSamples = FRAME_SIZE_SAMPLES;

    // Reset Opus decoder to clear internal state (prevents "corrupted stream" errors)
    if (m_decoder) {
        m_decoder.reset();
        m_decoder = std::make_unique<OpusDecoder>(SAMPLE_RATE, CHANNELS);
    }

    qDebug() << "AudioEngine::flushAudioBuffers - Flush completed";
}

void AudioEngine::allSamplesFlushed()
{
    qDebug() << "AudioEngine::allSamplesFlushed - All samples have been flushed";
}
