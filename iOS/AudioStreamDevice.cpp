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

#include "AudioStreamDevice.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <QDebug>

AudioStreamDevice::AudioStreamDevice(AudioJitterBuffer* jitterBuffer, Resampler* resampler, int outputSampleRate, QAudioFormat::SampleFormat sampleFormat, QObject *parent)
    : QIODevice(parent), m_jitterBuffer(jitterBuffer), m_outputResampler(resampler), m_outputSampleRate(outputSampleRate), m_sampleFormat(sampleFormat)
{
    open(QIODevice::ReadOnly);
    qDebug() << "AudioStreamDevice created: outputSampleRate=" << outputSampleRate 
             << "resampler=" << (resampler ? "present" : "null");
}

qint64 AudioStreamDevice::readData(char *data, qint64 maxSize)
{
    const int bytesPerSample = (m_sampleFormat == QAudioFormat::Int16) ? sizeof(qint16) : sizeof(float);
    if (maxSize <= 0)
        return 0;

    // How many output samples can the sink's buffer hold?
    const int samplesSinkCanHold = int(maxSize / bytesPerSample);
    if (samplesSinkCanHold <= 0)
        return 0;

    // We'll fill from the leftover tail first, then produce more if needed.
    qint64 bytesWritten = 0;
    int samplesWritten = 0;

    auto writeChunk = [&](const float* src, int count) {
        const int toWrite = std::min(count, samplesSinkCanHold - samplesWritten);
        if (toWrite <= 0) return 0;
        if (m_sampleFormat == QAudioFormat::Int16) {
            qint16* int16Data = reinterpret_cast<qint16*>(data) + samplesWritten;
            for (int i = 0; i < toWrite; ++i) {
                float s = std::max(-1.0f, std::min(1.0f, src[i]));
                int16Data[i] = static_cast<qint16>(s * 32767.0f);
            }
        } else {
            memcpy(reinterpret_cast<float*>(data) + samplesWritten, src, size_t(toWrite) * sizeof(float));
        }
        samplesWritten += toWrite;
        bytesWritten = qint64(samplesWritten) * bytesPerSample;
        return toWrite;
    };

    // 1) Drain any leftover resampled data from previous calls
    {
        QMutexLocker lock(&m_tailMutex);
        const int tailAvail = int(m_resampleTail.size() - m_tailPos);
        if (tailAvail > 0) {
            const int used = writeChunk(m_resampleTail.data() + m_tailPos, tailAvail);
            m_tailPos += size_t(used);
            if (m_tailPos >= m_resampleTail.size()) {
                m_resampleTail.clear();
                m_tailPos = 0;
            }
            if (samplesWritten >= samplesSinkCanHold)
                return bytesWritten; // buffer full; we're done
        }
    }

    // 2) If we still need more, produce it from the jitter buffer (native 16 kHz)
    // Estimate how many 16kHz samples needed for the remaining output.
    int remainingOut = samplesSinkCanHold - samplesWritten;
    if (remainingOut <= 0)
        return bytesWritten;

    int samplesToReadFromBuffer = remainingOut;
    if (m_outputResampler) {
        // Round up to account for fractional resampler ratios/latency.
        samplesToReadFromBuffer = std::max(1, (remainingOut * 16000 + (m_outputSampleRate - 1)) / m_outputSampleRate);
        // Add a tiny headroom so resampler can flush filter state without causing another read immediately.
        samplesToReadFromBuffer += 16;
    }

    std::vector<float> nativeSamples(samplesToReadFromBuffer);
    m_jitterBuffer->readSamples(nativeSamples.data(), samplesToReadFromBuffer);

    // 3) Resample if needed
    std::vector<float> resampledSamples;
    const std::vector<float>* finalSamples = &nativeSamples;
    if (m_outputResampler) {
        resampledSamples = m_outputResampler->process(nativeSamples.data(), int(nativeSamples.size()));
        finalSamples = &resampledSamples;
    }

    // 4) Write up to the sink capacity; stash any leftovers for next time
    const int produced = int(finalSamples->size());
    const int usedNow = writeChunk(finalSamples->data(), produced);
    if (usedNow < produced) {
        QMutexLocker lock(&m_tailMutex);
        // Append leftovers to tail (avoid O(n) erase from front by tracking m_tailPos)
        if (m_resampleTail.empty() || m_tailPos >= m_resampleTail.size()) {
            m_resampleTail.assign(finalSamples->begin() + usedNow, finalSamples->end());
            m_tailPos = 0;
        } else {
            // There is still some unread tail — append to it
            m_resampleTail.insert(m_resampleTail.end(),
                                  finalSamples->begin() + usedNow, finalSamples->end());
        }
    }

    return bytesWritten;
}

qint64 AudioStreamDevice::writeData(const char*, qint64)
{
    return -1;
}

qint64 AudioStreamDevice::bytesAvailable() const
{
    const int availableNativeSamples = m_jitterBuffer->samplesInBuffer();
    const int bytesPerSample = (m_sampleFormat == QAudioFormat::Int16) ? sizeof(qint16) : sizeof(float);

#if defined(Q_OS_IOS)
    // iOS prebuffering check: only log when prebuffering to monitor performance
    // Audio flows through normally to prevent silence issues
    const int minPrebufSamples = m_jitterBuffer->prebufSamples();
    if (availableNativeSamples > 0 && availableNativeSamples < minPrebufSamples) {
        static int logCount = 0;
        if (++logCount <= 5) { // Only log first 5 times to avoid spam
            qDebug() << "AudioStreamDevice: iOS prebuffering - have" << availableNativeSamples << "need" << minPrebufSamples;
        }
    }
#endif

    // Include any leftover tail we already produced.
    int tailSamples = 0;
    {
        QMutexLocker lock(&m_tailMutex);
        tailSamples = int(m_resampleTail.size() - m_tailPos);
        if (tailSamples < 0) tailSamples = 0;
    }

    if (m_outputResampler) {
        const int resampledFromBuffer = (availableNativeSamples * m_outputSampleRate) / 16000;
        return qint64(resampledFromBuffer + tailSamples) * bytesPerSample;
    }
    // No resampler
    return qint64(availableNativeSamples + tailSamples) * bytesPerSample;
}

void AudioStreamDevice::triggerReadyRead()
{
    // Always trigger readyRead when we get new data
    // The readData method handles the case when there's insufficient data
    emit readyRead();
}

