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
#include "AndroidAudioTrackOutput.h"
#include <QDebug>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>
#include <cmath>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniObject>
#endif

namespace {
constexpr float kMeterFloorDb = -60.0f;
constexpr float kMeterSilenceThreshold = 0.001f;
constexpr qint64 kMeterPeakHoldDurationMs = 320;
constexpr qint64 kMeterDecayIdleDelayMs = 120;
constexpr qint64 kMeterResetDelayMs = 450;
constexpr float kMeterLevelDecayFactor = 0.78f;
constexpr float kMeterPeakDecayFactor = 0.90f;
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_audioReady(false)
    , m_recording(false)
    , m_audioSource(nullptr)
    , m_audioSink(nullptr)
    , m_audioInputDevice(nullptr)
    , m_audioRecoveryTimer(nullptr)
    , m_audioFocusLost(false)
    , m_audioFocusPaused(false)
{
    // Initialize audio recovery timer
    m_audioRecoveryTimer = new QTimer(this);
    m_audioRecoveryTimer->setInterval(2000); // Check every 2 seconds
    connect(m_audioRecoveryTimer, &QTimer::timeout, this, &AudioEngine::onAudioRecoveryTimer);

    m_meterDecayTimer = new QTimer(this);
    m_meterDecayTimer->setInterval(60);
    connect(m_meterDecayTimer, &QTimer::timeout, this, &AudioEngine::onMeterDecayTimer);

    // Pre-allocate buffers for performance optimization
    m_reusableFloatBuffer.reserve(8192);  // Reserve space for large audio chunks
    m_reusableOpusBuffer.resize(OPUS_BUFFER_SIZE);
    m_transcriptionPcmBuffer.reserve(FRAME_SIZE_SAMPLES);
    m_txLeadInSilenceFrame.assign(FRAME_SIZE_SAMPLES, 0.0f);
    m_txStartupBuffer.reserve(FRAME_SIZE_SAMPLES * 4);
}

AudioEngine::~AudioEngine()
{
    cleanupAudio();
}

float AudioEngine::decibelsToLinear(float levelDb)
{
    return std::pow(10.0f, levelDb / 20.0f);
}

float AudioEngine::clampAudioSample(float sample)
{
    return std::clamp(sample, -1.0f, 1.0f);
}

float AudioEngine::meterLevelFromAmplitude(float amplitude)
{
    if (amplitude <= kMeterSilenceThreshold) {
        return 0.0f;
    }

    const float decibels = std::clamp(20.0f * std::log10(amplitude), kMeterFloorDb, 0.0f);
    return (decibels - kMeterFloorDb) / std::abs(kMeterFloorDb);
}

void AudioEngine::applyRxGain(float* samples, int count)
{
    if (samples == nullptr || count <= 0 || m_rxGainMultiplier == 1.0f) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        samples[i] = clampAudioSample(samples[i] * m_rxGainMultiplier);
    }
}

void AudioEngine::applyTxGain(float* samples, int count)
{
    if (samples == nullptr || count <= 0 || m_txGainMultiplier == 1.0f) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        samples[i] *= m_txGainMultiplier;
    }
}

void AudioEngine::updateMeterState(float level, float peak, float& currentLevel, float& currentPeak,
                                   qint64& lastUpdateMs, qint64& peakHoldUntilMs,
                                   void (AudioEngine::*signal)(float, float))
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    currentLevel = level;
    lastUpdateMs = now;

    if (peak >= currentPeak) {
        currentPeak = peak;
        peakHoldUntilMs = now + kMeterPeakHoldDurationMs;
    } else if (now > peakHoldUntilMs) {
        currentPeak = std::max(peak, currentPeak * kMeterPeakDecayFactor);
        if (currentPeak < 0.01f) {
            currentPeak = 0.0f;
        }
    }

    emit (this->*signal)(currentLevel, currentPeak);
}

void AudioEngine::decayMeterState(float& currentLevel, float& currentPeak, qint64& lastUpdateMs,
                                  qint64& peakHoldUntilMs, void (AudioEngine::*signal)(float, float))
{
    if (currentLevel <= 0.0f && currentPeak <= 0.0f) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastUpdateMs > 0 && now - lastUpdateMs < kMeterDecayIdleDelayMs) {
        return;
    }

    if (lastUpdateMs > 0 && now - lastUpdateMs >= kMeterResetDelayMs) {
        currentLevel = 0.0f;
        currentPeak = 0.0f;
        lastUpdateMs = 0;
        peakHoldUntilMs = 0;
        emit (this->*signal)(0.0f, 0.0f);
        return;
    }

    currentLevel *= kMeterLevelDecayFactor;
    if (now > peakHoldUntilMs) {
        currentPeak *= kMeterPeakDecayFactor;
    }

    if (currentLevel < 0.01f) {
        currentLevel = 0.0f;
    }
    if (currentPeak < 0.01f) {
        currentPeak = 0.0f;
    }

    emit (this->*signal)(currentLevel, currentPeak);
}

void AudioEngine::updateRxMeter(const float* samples, int count)
{
    if (samples == nullptr || count <= 0) {
        return;
    }

    float sumSquares = 0.0f;
    float peakAmplitude = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float sample = samples[i];
        sumSquares += sample * sample;
        peakAmplitude = std::max(peakAmplitude, std::abs(sample));
    }

    const float rmsAmplitude = std::sqrt(sumSquares / static_cast<float>(count));
    updateMeterState(meterLevelFromAmplitude(rmsAmplitude),
                     meterLevelFromAmplitude(peakAmplitude),
                     m_rxMeterLevel, m_rxMeterPeakLevel,
                     m_rxMeterLastUpdateMs, m_rxMeterPeakHoldUntilMs,
                     &AudioEngine::rxMeterLevelsChanged);
}

void AudioEngine::updateTxMeter(const float* samples, int count)
{
    if (samples == nullptr || count <= 0) {
        return;
    }

    float sumSquares = 0.0f;
    float peakAmplitude = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float sample = samples[i];
        sumSquares += sample * sample;
        peakAmplitude = std::max(peakAmplitude, std::abs(sample));
    }

    const float rmsAmplitude = std::sqrt(sumSquares / static_cast<float>(count));
    updateMeterState(meterLevelFromAmplitude(rmsAmplitude),
                     meterLevelFromAmplitude(peakAmplitude),
                     m_txMeterLevel, m_txMeterPeakLevel,
                     m_txMeterLastUpdateMs, m_txMeterPeakHoldUntilMs,
                     &AudioEngine::txMeterLevelsChanged);
}

void AudioEngine::resetRxMeter()
{
    m_rxMeterLevel = 0.0f;
    m_rxMeterPeakLevel = 0.0f;
    m_rxMeterLastUpdateMs = 0;
    m_rxMeterPeakHoldUntilMs = 0;
    emit rxMeterLevelsChanged(0.0f, 0.0f);
}

void AudioEngine::resetTxMeter()
{
    m_txMeterLevel = 0.0f;
    m_txMeterPeakLevel = 0.0f;
    m_txMeterLastUpdateMs = 0;
    m_txMeterPeakHoldUntilMs = 0;
    emit txMeterLevelsChanged(0.0f, 0.0f);
}

void AudioEngine::setRxAudioLevelDb(float levelDb)
{
    const float normalizedLevel = std::clamp(levelDb, 0.0f, 9.0f);
    if (qFuzzyCompare(m_rxAudioLevelDb + 1.0f, normalizedLevel + 1.0f)) {
        return;
    }
    m_rxAudioLevelDb = normalizedLevel;
    m_rxGainMultiplier = decibelsToLinear(normalizedLevel);
    qDebug() << "AudioEngine: RX boost set to" << normalizedLevel << "dB"
             << "(" << m_rxGainMultiplier << "x )";
}

void AudioEngine::setTxAudioLevelDb(float levelDb)
{
    const float normalizedLevel = std::clamp(levelDb, -12.0f, 12.0f);
    if (qFuzzyCompare(m_txAudioLevelDb + 1.0f, normalizedLevel + 1.0f)) {
        return;
    }
    m_txAudioLevelDb = normalizedLevel;
    m_txGainMultiplier = decibelsToLinear(normalizedLevel);
    qDebug() << "AudioEngine: TX mic level set to" << normalizedLevel << "dB"
             << "(" << m_txGainMultiplier << "x )";
}

void AudioEngine::onMeterDecayTimer()
{
    decayMeterState(m_rxMeterLevel, m_rxMeterPeakLevel,
                    m_rxMeterLastUpdateMs, m_rxMeterPeakHoldUntilMs,
                    &AudioEngine::rxMeterLevelsChanged);
    decayMeterState(m_txMeterLevel, m_txMeterPeakLevel,
                    m_txMeterLastUpdateMs, m_txMeterPeakHoldUntilMs,
                    &AudioEngine::txMeterLevelsChanged);
}

void AudioEngine::initializeAudioComponents()
{
    // Create Opus encoder/decoder - they are thread-safe
    m_encoder = std::make_unique<OpusEncoder>(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP);
    m_encoder->applySvxlinkDefaults();
    m_decoder = std::make_unique<OpusDecoder>(SAMPLE_RATE, CHANNELS);

    // Initialize jitter buffer with enough headroom for bursty Android scheduling.
    m_jitterBuffer.setSize(FRAME_SIZE_SAMPLES * m_maxBufferFrames);
    // Start playback after 150 ms of buffered audio, aligned with mainstream VoIP defaults.
    const int prebufFrames = (150 / FRAME_SIZE_MS); // 20 ms per frame
    m_jitterBuffer.setPrebufSamples(FRAME_SIZE_SAMPLES * prebufFrames);
}

void AudioEngine::setupAudio()
{
    if (m_meterDecayTimer && !m_meterDecayTimer->isActive()) {
        m_meterDecayTimer->start();
    }

    const bool hasQtOutput = (m_audioSink != nullptr);
    const bool hasAndroidOutput = usesAndroidNativeOutput();
    if (!hasQtOutput && !hasAndroidOutput) {
        // Initialize audio components
        initializeAudioComponents();

        // Configure audio for VoIP before creating devices
        configureAudioForVoIP();

#if defined(Q_OS_ANDROID)
        if (startAndroidPlaybackOutput()) {
            setupAudioInput();

            if (!m_audioReady) {
                m_audioReady = true;
                emit audioReadyChanged(true);
                qDebug() << "AudioEngine: Android AudioTrack output started - AudioReady set to true";
            }

            emit audioSetupFinished();
            qDebug() << "AudioEngine::setupAudio - Android native audio output setup completed";
            return;
        }

        qWarning() << "AudioEngine: Android native playback failed; no Qt audio fallback is allowed";
        return;
#else
        // Create all Qt audio output objects
        const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

        QAudioFormat outFormat;
        outFormat.setSampleRate(SAMPLE_RATE);
        outFormat.setChannelCount(CHANNELS);
        outFormat.setSampleFormat(QAudioFormat::Float);

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
#endif
    } else {
        // Audio output already exists (e.g., after reconnection), ensure audioReady is set
        if (!m_audioReady) {
            m_audioReady = true;
            emit audioReadyChanged(true);
        }
        emit audioSetupFinished();
    }

    qDebug() << "AudioEngine::setupAudio - Audio output setup completed";
}

void AudioEngine::setupAudioInput()
{
    if (m_meterDecayTimer && !m_meterDecayTimer->isActive()) {
        m_meterDecayTimer->start();
    }

    // Request RX audio focus for capture pre-warming
#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
            "yo6say/latry/LatryActivity", "requestAudioFocusForRX", "()V");

    if (!m_androidAudioRecordInput) {
        // Use queued callbacks to marshal capture-thread samples onto the
        // AudioEngine's Qt event-loop thread.  This is critical: the same
        // object is later reused by startAndroidCaptureInput() for TX
        // recording, and the Opus encoder / pending-sample buffers are NOT
        // thread-safe.  Direct callbacks from the capture std::thread would
        // race with Qt-thread access (sendTxStartupLeadIn, stopRecording,
        // meter decay timer, etc.) and can corrupt the SILK resampler state
        // inside opus_encode_float → celt_fatal → SIGABRT.
        m_androidAudioRecordInput = std::make_unique<AndroidAudioRecordInput>(
            [this](const short* samples, int count, int sampleRate) {
                queueCapturedInt16Samples(samples, count, sampleRate);
            },
            [this](float* samples, int count, int sampleRate) {
                queueCapturedNativeFloatSamples(samples, count, sampleRate);
            });
    }
    m_androidAudioRecordInput->prepare();
    return;
#else
    if (!m_audioSource) {
        m_inputResampler.reset();
        m_inputFormat = QAudioFormat();
        m_pendingInputSamples.clear();
        resetTxStartupPriming();

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
        const QList<int> channelCounts = {CHANNELS, 2};

        for (int sampleRate : sampleRates) {
            for (auto sampleFormat : sampleFormats) {
                for (int channels : channelCounts) {
                    inFormat.setSampleRate(sampleRate);
                    inFormat.setChannelCount(channels);
                    inFormat.setSampleFormat(sampleFormat);

                    if (inputDevice.isFormatSupported(inFormat)) {
                        qDebug() << "Found supported input format:" << sampleRate << "Hz," << channels << "channels," << sampleFormat;
                        formatFound = true;

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
                                   (inFormat.sampleFormat() == QAudioFormat::Int32) ? 4 : 4;
        const int channels = inFormat.channelCount();
        const int bufferDurationMs = 200;

        int optimalBufferSize = (samplesPerSecond * bufferDurationMs / 1000) * channels * bytesPerSample;
        m_audioSource->setBufferSize(optimalBufferSize);

        m_inputFormat = inFormat;
        qDebug() << "AudioEngine::setupAudioInput - Audio source created successfully with format:"
                 << inFormat.sampleRate() << "Hz," << inFormat.channelCount() << "channels," << inFormat.sampleFormat()
                 << "Buffer size:" << optimalBufferSize << "bytes (" << bufferDurationMs << "ms)";
    } else {
        qDebug() << "AudioEngine::setupAudioInput - Audio source already exists";
    }
#endif
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
    if (m_meterDecayTimer) {
        m_meterDecayTimer->stop();
    }

    // Stop recording safely
    if (m_recording) {
        stopRecording();
    }

    stopAndroidPlaybackOutput();
    releaseAndroidCaptureInput();

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
    m_inputResampler.reset();
    m_inputFormat = QAudioFormat();
    m_pendingInputSamples.clear();
    setTranscriptionPipeFd(-1);
    resetTxStartupPriming();

    // Reset state
    m_audioReady = false;
    resetRxMeter();
    resetTxMeter();

    qDebug() << "AudioEngine::cleanupAudio() - Cleanup completed";
}

void AudioEngine::cleanup()
{
    qDebug() << "AudioEngine: Explicit cleanup requested";
    cleanupAudio();
}
