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
#include "OpusWrapper.h"
#include "Resampler.h"
#include "AudioJitterBuffer.h"
#include "AudioStreamDevice.h"
#include <memory>
#include <vector>

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

public slots:
    void setupAudio();
    void setupAudioInput();
    void restartAudio();
    void startRecording();
    void stopRecording();
    void processReceivedAudio(const QByteArray &audioData, quint16 sequence);
    void processReceivedAudioDirect(const unsigned char* audioData, int size, quint16 sequence);
    void flushAudioBuffers();
    void cleanup();
    void checkAudioHealth();
    void onAudioFocusLost();
    void onAudioFocusPaused();
    void onAudioFocusGained();
    void onActivityPaused();
    void onActivityResumed();
    void allSamplesFlushed();

signals:
    void audioReadyChanged(bool ready);
    void audioDataEncoded(const QByteArray &encodedData);
    void audioSetupFinished();
    void audioRecoveryNeeded();

private slots:
    void onAudioInputReadyRead();
    void onAudioRecoveryTimer();
    void onAudioRouteChanged();

private:
    void initializeAudioComponents();
    void cleanupAudio();
    void maintainAudioSink();
    void configureAudioForVoIP();
    void resetAudioMode();

    bool m_audioReady = false;
    bool m_recording = false;

    // Audio I/O
    QAudioSource* m_audioSource = nullptr;
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_audioInputDevice = nullptr;
    QIODevice* m_audioOutputDevice = nullptr;
    QAudioFormat m_inputFormat;
    QAudioFormat m_outputFormat;

    // Audio processing components
    std::unique_ptr<OpusEncoder> m_encoder;
    std::unique_ptr<OpusDecoder> m_decoder;
    std::unique_ptr<Resampler> m_outputResampler;
    std::unique_ptr<Resampler> m_inputResampler;
    std::vector<float> m_pendingInputSamples;

    // Audio buffering and pacing
    AudioStreamDevice* m_audioStreamDevice = nullptr;
    AudioJitterBuffer m_jitterBuffer;
    const int m_maxBufferFrames = 24; // 480ms headroom (0.0.6 working value)
    quint16 m_lastAudioSeq = 0;

    // Audio focus management (Android)
    QTimer* m_audioRecoveryTimer = nullptr;
    bool m_audioFocusLost = false;
    bool m_audioFocusPaused = false;
    QDateTime m_lastAudioWrite;
    
    // Pre-allocated buffers for performance optimization
    std::vector<float> m_reusableFloatBuffer;
    std::vector<unsigned char> m_reusableOpusBuffer;
    static constexpr int OPUS_BUFFER_SIZE = 4000;
    
    class AudioLimiter {
    public:
        AudioLimiter() : thresholdDb_(-6.0), ratio_(0.1), outputGain_(1.0), envDb_(1.0E-25) {
        }
        
        void processAudio(float* samples, int count);
        
    private:
        double thresholdDb_;  // -6dB threshold for FM
        double ratio_;        // 0.1 = 10:1 compression ratio
        double outputGain_;   // Output gain
        double envDb_;        // Envelope detector state
        
        // Fast attack/slow release envelope detector
        static constexpr double ATTACK_COEF = 0.99;   // ~2ms attack
        static constexpr double RELEASE_COEF = 0.9995; // ~20ms release
        static constexpr double DC_OFFSET = 1.0E-25;
        
        inline double lin2dB(double lin) const {
            static const double LOG_2_DB = 8.6858896380650365530225783783321;
            return log(lin) * LOG_2_DB;
        }
        
        inline double dB2lin(double dB) const {
            static const double DB_2_LOG = 0.11512925464970228420089957273422;
            return exp(dB * DB_2_LOG);
        }
    };
    
    AudioLimiter m_audioLimiter;

#if defined(Q_OS_ANDROID)
    bool m_requestingMicPerm = false;
#endif
};

#endif // AUDIOENGINE_H