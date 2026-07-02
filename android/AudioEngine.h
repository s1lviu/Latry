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

#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioSource>
#include <QAudioSink>
#include <QTimer>
#include <QDateTime>
#include <cstdint>
#include "OpusWrapper.h"
#include "Resampler.h"
#include "AudioJitterBuffer.h"
#include "AudioStreamDevice.h"
#include "AudioLimiter.h"
#include <memory>
#include <vector>

class AndroidAudioTrackOutput;
class AndroidAudioRecordInput;

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    // Audio configuration constants
    static inline const int SAMPLE_RATE = 16000;
    static inline const int CHANNELS = 1;
    static inline const int FRAME_SIZE_MS = 20;
    static inline const int FRAME_SIZE_SAMPLES = SAMPLE_RATE * FRAME_SIZE_MS / 1000;
    // Maximum frame size to support SVXLink clients with up to 60ms frames
    static inline const int MAX_FRAME_SIZE_SAMPLES = SAMPLE_RATE * 60 / 1000;

    bool isAudioReady() const { return m_audioReady; }
    bool isRecording() const { return m_recording; }

public slots:
    void setupAudio();
    void setupAudioInput();
    void restartAudio();
    void startRecording();
    void stopRecording();
    void processReceivedAudio(const QByteArray &audioData, quint16 sequence);
    void flushAudioBuffers();
    void cleanup();
    void checkAudioHealth();
    void onAudioFocusLost();
    void onAudioFocusPaused();
    void onAudioFocusGained();
    void onActivityPaused();
    void onActivityResumed();
    void handleAudioRouteChanged();
    void setRxAudioLevelDb(float levelDb);
    void setTxAudioLevelDb(float levelDb);
    void setTranscriptionPipeFd(int fd);
    void allSamplesFlushed();

signals:
    void audioReadyChanged(bool ready);
    void audioDataEncoded(const QByteArray &encodedData);
    void audioSetupFinished();
    void audioRecoveryNeeded();
    void txDrainComplete();
    void rxMeterLevelsChanged(float level, float peakLevel);
    void txMeterLevelsChanged(float level, float peakLevel);

private slots:
    void onAudioInputReadyRead();
    void onAudioRecoveryTimer();
    void onMeterDecayTimer();

private:
    friend class AudioEngineTest;

    void initializeAudioComponents();
    void cleanupAudio();
    void configureAudioForVoIP();
    void resetAudioMode();
    static float decibelsToLinear(float levelDb);
    static float clampAudioSample(float sample);
    static float meterLevelFromAmplitude(float amplitude);
    void applyRxGain(float* samples, int count);
    void applyTxGain(float* samples, int count);
    void updateRxMeter(const float* samples, int count);
    void updateTxMeter(const float* samples, int count);
    void resetRxMeter();
    void resetTxMeter();
    void updateMeterState(float level, float peak, float& currentLevel, float& currentPeak,
                          qint64& lastUpdateMs, qint64& peakHoldUntilMs,
                          void (AudioEngine::*signal)(float, float));
    void decayMeterState(float& currentLevel, float& currentPeak, qint64& lastUpdateMs,
                         qint64& peakHoldUntilMs, void (AudioEngine::*signal)(float, float));
    bool startAndroidPlaybackOutput();
    void stopAndroidPlaybackOutput();
    void pauseAndroidPlaybackOutput();
    void resumeAndroidPlaybackOutput();
    bool applyCurrentAndroidPlaybackRoute();
    bool usesAndroidNativeOutput() const;
    bool startAndroidCaptureInput();
    void stopAndroidCaptureInput();
    void releaseAndroidCaptureInput();
    bool usesAndroidNativeInput() const;
    void flushPendingTxSamples();
    void processCapturedFloatSamples(float* samples, int count);
    void processCapturedNativeFloatSamples(float* samples, int count, int sampleRate);
    void processCapturedInt16Samples(const short* samples, int count, int sampleRate);
    void queueCapturedNativeFloatSamples(const float* samples, int count, int sampleRate);
    void queueCapturedInt16Samples(const short* samples, int count, int sampleRate);
    int encodeTxFrame(const float* frameSamples);
    void encodeReadyTxFrames(const char* logContext);
    void prepareTxStartupPriming();
    void sendTxStartupLeadIn();
    void flushBufferedTxStartupAudio();
    void resetTxStartupPriming();

    bool m_audioReady = false;
    bool m_recording = false;

    // Audio I/O
    QAudioSource* m_audioSource = nullptr;
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_audioInputDevice = nullptr;
    QAudioFormat m_inputFormat;
    QAudioFormat m_outputFormat;

    // Audio processing components
    std::unique_ptr<OpusEncoder> m_encoder;
    std::unique_ptr<OpusDecoder> m_decoder;
    std::unique_ptr<Resampler> m_outputResampler;
    std::unique_ptr<Resampler> m_inputResampler;
    std::vector<float> m_pendingInputSamples;
    std::vector<float> m_txStartupBuffer;
    std::vector<float> m_txLeadInSilenceFrame;
    bool m_txStartupPrimingActive = false;
    int m_txStartupPrimingTargetSamples = 0;

    // Audio buffering and pacing
    AudioStreamDevice* m_audioStreamDevice = nullptr;
    AudioJitterBuffer m_jitterBuffer;
    const int m_maxBufferFrames = 24; // 480ms headroom (0.0.6 working value)
    bool m_hasLastAudioSeq = false;
    quint16 m_lastAudioSeq = 0;
    int m_lastDecodedFrameSamples = FRAME_SIZE_SAMPLES;
    std::unique_ptr<AndroidAudioTrackOutput> m_androidAudioTrackOutput;
    std::unique_ptr<AndroidAudioRecordInput> m_androidAudioRecordInput;

    // Audio focus management (Android)
    QTimer* m_audioRecoveryTimer = nullptr;
    QTimer* m_meterDecayTimer = nullptr;
    bool m_audioFocusLost = false;
    bool m_audioFocusPaused = false;
    QDateTime m_lastAudioWrite;
    
    // Pre-allocated buffers for performance optimization
    std::vector<float> m_reusableFloatBuffer;
    std::vector<unsigned char> m_reusableOpusBuffer;
    std::vector<int16_t> m_transcriptionPcmBuffer;
    static constexpr int OPUS_BUFFER_SIZE = 4000;
    int m_transcriptionPipeFd = -1;

    float m_rxAudioLevelDb = 0.0f;
    float m_txAudioLevelDb = 0.0f;
    float m_rxGainMultiplier = 1.0f;
    float m_txGainMultiplier = 1.0f;
    float m_rxMeterLevel = 0.0f;
    float m_rxMeterPeakLevel = 0.0f;
    float m_txMeterLevel = 0.0f;
    float m_txMeterPeakLevel = 0.0f;
    qint64 m_rxMeterLastUpdateMs = 0;
    qint64 m_rxMeterPeakHoldUntilMs = 0;
    qint64 m_txMeterLastUpdateMs = 0;
    qint64 m_txMeterPeakHoldUntilMs = 0;

    AudioLimiter m_audioLimiter;
};

#endif // AUDIOENGINE_H
