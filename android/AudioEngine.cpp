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
#include <QAudioDevice>
#include <QMediaDevices>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <QDataStream>
#include <QtEndian>
#include <QTime>
#include <QDateTime>
#include <algorithm>
#include <opus.h>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniObject>
#endif

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_audioReady(false)
    , m_recording(false)
    , m_audioSource(nullptr)
    , m_audioSink(nullptr)
    , m_audioInputDevice(nullptr)
    , m_audioOutputDevice(nullptr)
    , m_audioRecoveryTimer(nullptr)
    , m_audioFocusLost(false)
    , m_audioFocusPaused(false)
{
    // Initialize audio recovery timer
    m_audioRecoveryTimer = new QTimer(this);
    m_audioRecoveryTimer->setInterval(2000); // Check every 2 seconds
    connect(m_audioRecoveryTimer, &QTimer::timeout, this, &AudioEngine::onAudioRecoveryTimer);
    
    // Pre-allocate buffers for performance optimization
    m_reusableFloatBuffer.reserve(8192);  // Reserve space for large audio chunks
    m_reusableOpusBuffer.resize(OPUS_BUFFER_SIZE);
}

AudioEngine::~AudioEngine()
{
    cleanupAudio();
}

void AudioEngine::initializeAudioComponents()
{
    // Create Opus encoder/decoder - they are thread-safe
    m_encoder = std::make_unique<OpusEncoder>(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO);
    m_encoder->applySvxlinkDefaults();
    m_decoder = std::make_unique<OpusDecoder>(SAMPLE_RATE, CHANNELS);
    
    // Initialize jitter buffer (0.0.6 working configuration: 480ms)
    m_jitterBuffer.setSize(FRAME_SIZE_SAMPLES * m_maxBufferFrames);
    // Keep at least 400 ms in buffer before playback (0.0.6 working value)
    const int prebufFrames = (400 / FRAME_SIZE_MS); // 20 ms per frame
    m_jitterBuffer.setPrebufSamples(FRAME_SIZE_SAMPLES * prebufFrames);
}

void AudioEngine::setupAudio()
{
    if (!m_audioSink) {
        // Initialize audio components
        initializeAudioComponents();
        
        // Configure audio for VoIP before creating devices
        configureAudioForVoIP();
        
        // Create all Qt audio output objects
        const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

        QAudioFormat outFormat;
        outFormat.setSampleRate(SAMPLE_RATE);
        outFormat.setChannelCount(CHANNELS);
#if defined(Q_OS_ANDROID)
        outFormat.setSampleFormat(QAudioFormat::Int16);  // Android prefers Int16
#else
        outFormat.setSampleFormat(QAudioFormat::Float);
#endif

        std::unique_ptr<Resampler> resampler;
        if (!outputDevice.isFormatSupported(outFormat)) {
            // Try 48kHz first — this is guaranteed to work with the FIR filters
            QAudioFormat alt = outFormat;
            alt.setSampleRate(48000);
            if (outputDevice.isFormatSupported(alt)) {
                qWarning() << "Using 48 kHz output format";
                outFormat = alt;
                resampler = std::make_unique<Resampler>(SAMPLE_RATE, 48000, CHANNELS);
            } else {
                qWarning() << "Falling back to preferred output format";
                outFormat = outputDevice.preferredFormat();
                if (outFormat.sampleRate() != SAMPLE_RATE)
                    resampler = std::make_unique<Resampler>(SAMPLE_RATE, outFormat.sampleRate(), CHANNELS);
            }
        }

        // Create sink and output device
        m_audioSink = new QAudioSink(outputDevice, outFormat, this);
        connect(m_audioSink, &QAudioSink::stateChanged, this, [](QAudio::State state){
            qDebug() << "Audio sink state changed to:" << state;
        });

        const int bytesPerSample = (outFormat.sampleFormat() == QAudioFormat::Int16) ? sizeof(qint16) : sizeof(float);
        const int outFrameSamples = outFormat.sampleRate() * FRAME_SIZE_MS / 1000;
        m_audioSink->setBufferSize(outFrameSamples * bytesPerSample * m_maxBufferFrames);
        // Create our custom IODevice bridge
        m_audioStreamDevice = new AudioStreamDevice(&m_jitterBuffer, m_outputResampler.get(), outFormat.sampleRate(), outFormat.sampleFormat(), this);

        // Audio focus will be requested in setupAudioInput when recording starts
        // This ensures proper timing for VoIP audio routing

        // Start the audio sink in pull mode
        m_audioSink->start(m_audioStreamDevice);
        qDebug() << "Audio sink started in pull mode.";

        // Store objects created in audio thread
        m_outputFormat = outFormat;
        if (resampler)
            m_outputResampler = std::move(resampler);

        // Set audio ready flag
        if (!m_audioReady) {
            m_audioReady = true;
            emit audioReadyChanged(true);
            qDebug() << "AudioEngine: Audio setup completed - AudioReady set to true";
        }

        emit audioSetupFinished();
    } else {
        // Audio sink already exists (e.g., after reconnection), ensure audioReady is set
        if (!m_audioReady) {
            m_audioReady = true;
            emit audioReadyChanged(true);
        }
    }

    // Audio output setup is complete - input setup is done separately on demand
    qDebug() << "AudioEngine::setupAudio - Audio output setup completed";
}

void AudioEngine::setupAudioInput()
{

    // Request audio focus and set communication mode for VoIP
#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
#endif

    if (!m_audioSource) {
        const QAudioDevice &inputDevice = QMediaDevices::defaultAudioInput();
        if (inputDevice.isNull()) {
            qWarning() << "No audio input device available";
            return;
        }

        QAudioFormat inFormat;
        bool formatFound = false;

        // Try multiple format combinations to find one that works
        const QList<int> sampleRates = {SAMPLE_RATE, 48000, 44100, 22050, 8000};
        const QList<QAudioFormat::SampleFormat> sampleFormats = {
            QAudioFormat::Int16, QAudioFormat::Float, QAudioFormat::Int32
        };
        const QList<int> channelCounts = {CHANNELS, 2}; // Try mono first, then stereo

        // First, try our preferred combinations
        for (int sampleRate : sampleRates) {
            for (auto sampleFormat : sampleFormats) {
                for (int channels : channelCounts) {
                    inFormat.setSampleRate(sampleRate);
                    inFormat.setChannelCount(channels);
                    inFormat.setSampleFormat(sampleFormat);
                    
                    if (inputDevice.isFormatSupported(inFormat)) {
                        qDebug() << "Found supported input format:" << sampleRate << "Hz," << channels << "channels," << sampleFormat;
                        formatFound = true;
                        
                        // Set up resampler if needed (we always output mono after channel mixing)
                        if (sampleRate != SAMPLE_RATE) {
                            m_inputResampler = std::make_unique<Resampler>(sampleRate, SAMPLE_RATE, CHANNELS);
                        }
                        break;
                    }
                }
                if (formatFound) break;
            }
            if (formatFound) break;
        }

        // If no format worked, try the device's preferred format
        if (!formatFound) {
            qWarning() << "No standard format supported, trying device preferred format";
            inFormat = inputDevice.preferredFormat();
            
            if (inFormat.isValid()) {
                qDebug() << "Using preferred format:" << inFormat.sampleRate() << "Hz," 
                         << inFormat.channelCount() << "channels," << inFormat.sampleFormat();
                         
                // Set up resampler if needed (we always output mono after channel mixing)
                if (inFormat.sampleRate() != SAMPLE_RATE) {
                    m_inputResampler = std::make_unique<Resampler>(inFormat.sampleRate(), SAMPLE_RATE, CHANNELS);
                }
                formatFound = true;
            }
        }

        if (!formatFound) {
            qWarning() << "No supported audio input format found on this device";
            return;
        }

        m_audioSource = new QAudioSource(inputDevice, inFormat, this);
        
        // Set optimal buffer size for network resilience
        const int samplesPerSecond = inFormat.sampleRate();
        const int bytesPerSample = (inFormat.sampleFormat() == QAudioFormat::Int16) ? 2 : 
                                   (inFormat.sampleFormat() == QAudioFormat::Int32) ? 4 : 4; // Float = 4 bytes
        const int channels = inFormat.channelCount();
        const int bufferDurationMs = 200; // 200ms buffer for network resilience
        
        int optimalBufferSize = (samplesPerSecond * bufferDurationMs / 1000) * channels * bytesPerSample;
        m_audioSource->setBufferSize(optimalBufferSize);
        
        m_inputFormat = inFormat;
        qDebug() << "AudioEngine::setupAudioInput - Audio source created successfully with format:" 
                 << inFormat.sampleRate() << "Hz," << inFormat.channelCount() << "channels," << inFormat.sampleFormat()
                 << "Buffer size:" << optimalBufferSize << "bytes (" << bufferDurationMs << "ms)";
    } else {
        qDebug() << "AudioEngine::setupAudioInput - Audio source already exists";
    }
}

void AudioEngine::restartAudio()
{
    qDebug() << "Restarting audio due to focus recovery";
    
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
    if (m_audioSink) {
        if (m_audioSink->state() == QAudio::SuspendedState || 
            m_audioSink->state() == QAudio::StoppedState) {
            qDebug() << "Audio sink is suspended/stopped, requesting restart";
            restartAudio();
        }
        
        // Maintain audio sink state - not needed in pull mode
    }
    
    // Check if we haven't received audio in a while (more than 5 seconds)
    if (m_lastAudioWrite.isValid() && 
        m_lastAudioWrite.secsTo(QDateTime::currentDateTime()) > 5) {
        qDebug() << "No audio received for 5+ seconds, checking focus";
        
        // Flush jitter buffer (SVXLink equivalent of flushEncodedSamples)
        m_jitterBuffer.clear();
        
        // Reset audio sequence tracking
        m_lastAudioSeq = 0;
        
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

void AudioEngine::startRecording()
{
    qDebug() << "AudioEngine::startRecording called - audioSource:" << (m_audioSource ? "OK" : "NULL") 
             << "recording:" << m_recording << "audioReady:" << m_audioReady;
             
    if (!m_audioSource) {
        qWarning() << "AudioEngine::startRecording - No audio source available, trying to setup audio input first";
        setupAudioInput();
        if (!m_audioSource) {
            qWarning() << "AudioEngine::startRecording - Audio source still not available after setup, retrying in 200ms";
            // Retry after a short delay to allow audio system to initialize
            QTimer::singleShot(200, this, [this]() {
                setupAudioInput();
                if (m_audioSource) {
                    qDebug() << "AudioEngine::startRecording - Audio source available after retry, starting recording";
                    startRecording();
                } else {
                    qWarning() << "AudioEngine::startRecording - Audio source still not available after retry";
                }
            });
            return;
        }
    }
    
    if (m_recording) {
        qDebug() << "AudioEngine::startRecording - Already recording";
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
    if (!m_audioSource || !m_recording) {
        return;
    }
    
    // Set recording flag to false first to prevent new processing
    m_recording = false;
    
    // Disconnect signal immediately to prevent further callbacks during cleanup
    if (m_audioInputDevice) {
        disconnect(m_audioInputDevice, &QIODevice::readyRead, this, &AudioEngine::onAudioInputReadyRead);
    }
    
    // Reset audio mode when stopping recording
    resetAudioMode();
    
#if defined(Q_OS_ANDROID)
    // On Android with OpenSL ES, the most reliable way to avoid mutex timeouts
    // is to recreate the audio source for each recording session
    // This avoids the problematic stop/start cycle that causes mutex issues
    if (m_audioSource) {
        // Don't try to stop - just delete and recreate next time
        QMetaObject::invokeMethod(m_audioSource, "deleteLater", Qt::QueuedConnection);
        m_audioSource = nullptr;
        m_audioInputDevice = nullptr;
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
}

void AudioEngine::processReceivedAudio(const QByteArray &audioData, quint16 sequence)
{
    if (!m_decoder || !m_audioReady) {
        qDebug() << "AudioEngine::processReceivedAudio - Audio not ready, skipping"
                 << "decoder:" << (m_decoder ? "OK" : "NULL")
                 << "audioReady:" << m_audioReady;
        return;
    }
    
    // Handle missing packets with packet loss concealment
    if (m_lastAudioSeq != 0) {
        quint16 expected = static_cast<quint16>(m_lastAudioSeq + 1);
        quint16 diff = static_cast<quint16>(sequence - expected);
        if (diff > 0) {
            unsigned missing = diff;
            while (missing--) {
                std::vector<float> plc(MAX_FRAME_SIZE_SAMPLES * CHANNELS);
                int plcSamples = m_decoder->decode(nullptr, 0, plc.data(), MAX_FRAME_SIZE_SAMPLES);
                if (plcSamples > 0) {
                    m_jitterBuffer.writeSamples(plc.data(), plcSamples);
                }
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
}

void AudioEngine::processReceivedAudioDirect(const unsigned char* audioData, int size, quint16 sequence)
{
    if (!m_decoder || !m_audioReady) {
        qDebug() << "AudioEngine::processReceivedAudioDirect - Audio not ready, skipping"
                 << "decoder:" << (m_decoder ? "OK" : "NULL")
                 << "audioReady:" << m_audioReady;
        return;
    }
    
    // Handle missing packets with packet loss concealment (SVXLink equivalent)
    if (m_lastAudioSeq != 0) {
        quint16 expected = static_cast<quint16>(m_lastAudioSeq + 1);
        quint16 diff = static_cast<quint16>(sequence - expected);
        if (diff > 0) {
            unsigned missing = diff;
            while (missing--) {
                std::vector<float> plc(MAX_FRAME_SIZE_SAMPLES * CHANNELS);
                int plcSamples = m_decoder->decode(nullptr, 0, plc.data(), MAX_FRAME_SIZE_SAMPLES);
                if (plcSamples > 0) {
                    m_jitterBuffer.writeSamples(plc.data(), plcSamples);
                }
            }
        }
    }
    
    // Decode the Opus audio data directly (SVXLink pattern) - hybrid approach for optimal performance
    // Try normal 20ms buffer first, fallback to larger buffer for v1 clients with 40/60ms frames
    std::vector<float> decodedSamples(FRAME_SIZE_SAMPLES * CHANNELS);
    int decodedSampleCount = m_decoder->decode(audioData, size, decodedSamples.data(), FRAME_SIZE_SAMPLES);
    
    // If buffer too small, retry with larger buffer for legacy clients
    if (decodedSampleCount == OPUS_BUFFER_TOO_SMALL) {
        decodedSamples.resize(MAX_FRAME_SIZE_SAMPLES * CHANNELS);
        decodedSampleCount = m_decoder->decode(audioData, size, decodedSamples.data(), MAX_FRAME_SIZE_SAMPLES);
    }
    
    if (decodedSampleCount > 0) {
        // Write the NATIVE 16kHz samples directly to the jitter buffer
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
}

void AudioEngine::flushAudioBuffers()
{
    qDebug() << "AudioEngine::flushAudioBuffers - Starting flush";
    
    // Clear jitter buffer
    m_jitterBuffer.clear();
    
    // Reset last audio sequence
    m_lastAudioSeq = 0;
    
    // Reset Opus decoder to clear internal state (prevents "corrupted stream" errors)
    if (m_decoder) {
        m_decoder.reset();
        m_decoder = std::make_unique<OpusDecoder>(SAMPLE_RATE, CHANNELS);
    }
    
    
    // Reset output device and flush audio pacer safely
    if (m_audioOutputDevice) {
        try {
            m_audioOutputDevice->reset();
        } catch (...) {
            qWarning() << "Exception during audioOutputDevice reset";
        }
    }
    
    qDebug() << "AudioEngine::flushAudioBuffers - Flush completed";
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
        
        // Resize reusable buffer only if needed (avoids frequent reallocations)
        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }
        
        if (inputChannels == 1) {
            // Optimized mono conversion using multiplication instead of division
            for (int i = 0; i < monoSamples; ++i) {
                m_reusableFloatBuffer[i] = src[i] * (1.0f / 32768.0f);
            }
        } else {
            // Optimized stereo/multi-channel mixing
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
        
        // Resize reusable buffer only if needed
        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }
        
        if (inputChannels == 1) {
            // Mono input - direct copy to reusable buffer
            std::copy(src, src + monoSamples, m_reusableFloatBuffer.data());
        } else {
            // Stereo or multi-channel input - mix down to mono
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
        
        // Resize reusable buffer only if needed
        if (m_reusableFloatBuffer.size() < monoSamples) {
            m_reusableFloatBuffer.resize(monoSamples);
        }
        
        if (inputChannels == 1) {
            // Optimized mono conversion using multiplication instead of division
            for (int i = 0; i < monoSamples; ++i) {
                m_reusableFloatBuffer[i] = src[i] * (1.0f / 2147483648.0f);
            }
        } else {
            // Optimized stereo/multi-channel mixing
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

    // Apply user-configurable microphone gain (-20dB to +20dB)
    // Create a modifiable copy for gain application if using resampled data
    std::vector<float> gainProcessedData;
    if (m_micGainLinear != 1.0f) {
        gainProcessedData.resize(samplesRead);
        for (int i = 0; i < samplesRead; ++i) {
            gainProcessedData[i] = sampleData[i] * m_micGainLinear;
        }
        sampleData = gainProcessedData.data();
    }

    // Apply SVXLink-style audio limiting for FM transmission (-6dBFS)
    m_audioLimiter.processAudio(const_cast<float*>(sampleData), samplesRead);

    // Efficient buffer management - reserve space to avoid frequent reallocations
    m_pendingInputSamples.reserve(m_pendingInputSamples.size() + samplesRead);
    m_pendingInputSamples.insert(m_pendingInputSamples.end(), sampleData, sampleData + samplesRead);

    // Use pre-allocated Opus buffer
    while(m_pendingInputSamples.size() >= FRAME_SIZE_SAMPLES) {
        int encodedBytes = m_encoder->encode(m_pendingInputSamples.data(), FRAME_SIZE_SAMPLES, 
                                           m_reusableOpusBuffer.data(), OPUS_BUFFER_SIZE);
        if (encodedBytes > 0) {
            QByteArray encodedData(reinterpret_cast<const char*>(m_reusableOpusBuffer.data()), encodedBytes);
            emit audioDataEncoded(encodedData);
            qDebug() << "AudioEngine::onAudioInputReadyRead - Encoded" << encodedBytes << "bytes, emitted audioDataEncoded signal";
        } else {
            qWarning() << "Opus encode error:" << opus_strerror(encodedBytes);
        }
        m_pendingInputSamples.erase(m_pendingInputSamples.begin(), m_pendingInputSamples.begin() + FRAME_SIZE_SAMPLES);
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
    if (m_audioSink) {
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
    if (m_audioSink) {
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
    qDebug() << "AudioEngine::onActivityPaused - Activity paused, cleaning up audio";
    
    // Immediately cleanup audio resources when activity is paused
    // This prevents segfaults when Android forcibly releases audio resources
    cleanupAudio();
}

void AudioEngine::onActivityResumed()
{
    qDebug() << "AudioEngine::onActivityResumed - Activity resumed";
    
    // Clear focus flags and restart audio if needed
    bool wasLost = m_audioFocusLost;
    m_audioFocusLost = false;
    m_audioFocusPaused = false;
    
    if (m_audioReady) {
        // Always re-request audio focus and restart audio when resuming while connected
        #if defined(Q_OS_ANDROID)
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        #endif
        QTimer::singleShot(100, this, &AudioEngine::restartAudio);
    } else if (wasLost) {
        // If we were disconnected, just clear the lost flag and prepare for future connection
        #if defined(Q_OS_ANDROID)
        QJniObject::callStaticMethod<void>("yo6say/latry/LatryActivity", "requestAudioFocus", "()V");
        #endif
    }
    
    // Start audio recovery timer
    if (m_audioRecoveryTimer && !m_audioRecoveryTimer->isActive()) {
        m_audioRecoveryTimer->start();
    }
}

void AudioEngine::allSamplesFlushed()
{
    qDebug() << "AudioEngine::allSamplesFlushed - All samples have been flushed";
    // This method is called when the server indicates all audio samples have been processed
    // Currently no specific action needed, just acknowledge the notification
}

void AudioEngine::cleanupAudio()
{
    qDebug() << "AudioEngine::cleanupAudio() - Starting cleanup";
    
    // Reset audio mode first
    resetAudioMode();
    
    // Stop timers first
    if (m_audioRecoveryTimer) {
        m_audioRecoveryTimer->stop();
    }
    
    // Stop recording safely
    if (m_recording) {
        stopRecording();
    }
    
    // AudioStreamDevice will be automatically cleaned up as it has 'this' as parent
    
    // Stop and cleanup audio sink safely
    if (m_audioSink) {
        try {
            if (m_audioSink->state() != QAudio::StoppedState) {
                m_audioSink->stop();
            }
            QMetaObject::invokeMethod(m_audioSink, "deleteLater", Qt::QueuedConnection);
            m_audioSink = nullptr;
        } catch (...) {
            qWarning() << "Exception during audioSink cleanup";
            m_audioSink = nullptr;
        }
    }
    
    // Stop and cleanup audio source safely
    if (m_audioSource) {
        try {
            if (m_audioSource->state() != QAudio::StoppedState) {
                m_audioSource->stop();
            }
            QMetaObject::invokeMethod(m_audioSource, "deleteLater", Qt::QueuedConnection);
            m_audioSource = nullptr;
        } catch (...) {
            qWarning() << "Exception during audioSource cleanup";
            m_audioSource = nullptr;
        }
    }
    
    // Clear device pointers
    m_audioInputDevice = nullptr;
    m_audioOutputDevice = nullptr;
    
    // Reset state
    m_audioReady = false;
    
    qDebug() << "AudioEngine::cleanupAudio() - Cleanup completed";
}

void AudioEngine::cleanup()
{
    qDebug() << "AudioEngine: Explicit cleanup requested";
    cleanupAudio();
}

void AudioEngine::maintainAudioSink()
{
    // No longer needed in pull mode - QAudioSink manages itself
}

void AudioEngine::configureAudioForVoIP()
{
#if defined(Q_OS_ANDROID)
    try {
        // Get Android context safely
        QJniObject context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative", "getContext", "()Landroid/content/Context;");
        
        if (!context.isValid()) {
            qWarning() << "AudioEngine: Failed to get Android context for VoIP configuration";
            return;
        }
        
        // Get AudioManager service
        QJniObject audioManager = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            QJniObject::fromString("audio").object());
        
        if (!audioManager.isValid()) {
            qWarning() << "AudioEngine: Failed to get AudioManager service";
            return;
        }
        
        // Check Android version for compatibility
        int sdkVersion = QJniObject::getStaticField<jint>(
            "android/os/Build$VERSION", "SDK_INT");
        
        if (sdkVersion >= 31) { // Android 12+
            // Use modern communication device API for Android 12+
            qDebug() << "AudioEngine: Using Android 12+ audio routing (SDK" << sdkVersion << ")";
            // when audio focus is properly requested
        } else {
            // Use legacy MODE_IN_COMMUNICATION for older Android versions
            audioManager.callMethod<void>("setMode", "(I)V", 3); // MODE_IN_COMMUNICATION
            qDebug() << "AudioEngine: Set MODE_IN_COMMUNICATION for VoIP (SDK" << sdkVersion << ")";
        }
        
    } catch (...) {
        qWarning() << "AudioEngine: Exception during VoIP audio configuration";
    }
#endif
    
    // Audio route change detection will be handled by proper audio focus management
    qDebug() << "AudioEngine: VoIP audio configuration completed";
}

void AudioEngine::resetAudioMode()
{
#if defined(Q_OS_ANDROID)
    try {
        // Get Android context safely
        QJniObject context = QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative", "getContext", "()Landroid/content/Context;");
        
        if (!context.isValid()) {
            qWarning() << "AudioEngine: Failed to get Android context for audio reset";
            return;
        }
        
        // Get AudioManager service  
        QJniObject audioManager = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            QJniObject::fromString("audio").object());
        
        if (!audioManager.isValid()) {
            qWarning() << "AudioEngine: Failed to get AudioManager for reset";
            return;
        }
        
        // Check Android version for compatibility
        int sdkVersion = QJniObject::getStaticField<jint>(
            "android/os/Build$VERSION", "SDK_INT");
        
        if (sdkVersion >= 31) { // Android 12+
            // Clear communication device on Android 12+
            qDebug() << "AudioEngine: Clearing communication device (Android 12+)";
            audioManager.callMethod<jboolean>("clearCommunicationDevice");
        } else {
            // Reset to MODE_NORMAL on older Android versions
            audioManager.callMethod<void>("setMode", "(I)V", 0); // MODE_NORMAL
            qDebug() << "AudioEngine: Reset to MODE_NORMAL (legacy Android)";
        }
        
    } catch (...) {
        qWarning() << "AudioEngine: Exception during audio mode reset";
    }
#endif
}

void AudioEngine::onAudioRouteChanged()
{
    qDebug() << "AudioEngine: Audio route changed - device connected/disconnected";
    
    // In communication mode, Android will automatically route audio to newly connected devices
    // This slot is mainly for logging and potential future enhancements
    if (m_audioSink && m_audioSink->state() == QAudio::ActiveState) {
        qDebug() << "AudioEngine: Audio output active - OS will handle route change automatically";
    }
    
    if (m_audioSource && m_recording) {
        qDebug() << "AudioEngine: Audio input active - OS will handle route change automatically";
    }
}

void AudioEngine::AudioLimiter::processAudio(float* samples, int count) {
    for (int i = 0; i < count; ++i) {
        double rectified = fabs(samples[i]);  // Rectify input signal
        rectified += DC_OFFSET;               // Add DC offset to avoid log(0)
        double keyDb = lin2dB(rectified);     // Convert linear to dB
        
        // Calculate overdB (how much above threshold)
        double overDb = keyDb - thresholdDb_;
        if (overDb < 0.0) {
            overDb = 0.0;
        }
        
        overDb += DC_OFFSET;  // Add DC offset to avoid denormal
        
        // Envelope detector with attack/release
        if (overDb > envDb_) {
            envDb_ = overDb + ATTACK_COEF * (envDb_ - overDb);   // Fast attack
        } else {
            envDb_ = overDb + RELEASE_COEF * (envDb_ - overDb);  // Slow release
        }
        
        overDb = envDb_ - DC_OFFSET;  // Remove DC offset
        
        // Calculate gain reduction using compression ratio
        double gainReductionDb = overDb * (ratio_ - 1.0);  // Gain reduction in dB
        double gainReduction = dB2lin(gainReductionDb);    // Convert to linear
        
        // Apply gain reduction to input sample
        samples[i] = outputGain_ * samples[i] * gainReduction;
    }
}

void AudioEngine::setMicGainDb(double gainDb)
{
    // Clamp gain to -20dB to +20dB range for safety
    gainDb = qBound(-20.0, gainDb, 20.0);
    
    if (qAbs(m_micGainDb - gainDb) < 0.1) return; // Avoid unnecessary updates
    
    m_micGainDb = gainDb;
    
    // Convert dB to linear gain factor: gain_linear = 10^(dB/20)
    m_micGainLinear = static_cast<float>(std::pow(10.0, gainDb / 20.0));
    
    qDebug() << "AudioEngine: Android microphone gain updated to" << gainDb << "dB (linear:" << m_micGainLinear << ")";
}