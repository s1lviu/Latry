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
    
    // How many output samples can the sink's buffer hold?
    const int samplesSinkCanHold = maxSize / bytesPerSample;
    
    // How many output samples do we actually have ready?
    const int samplesWeHave = bytesAvailable() / bytesPerSample;
    
    // We can only read the minimum of what the sink wants and what we have
    const int samplesToGenerate = std::min(samplesSinkCanHold, samplesWeHave);
    if (samplesToGenerate == 0) {
        return 0; // It's OK to return 0 here. The sink will wait for the next readyRead.
    }

    // How many 16kHz samples do we need to pull to generate that many output samples?
    int samplesToReadFromBuffer;
    if (m_outputResampler) {
        samplesToReadFromBuffer = (samplesToGenerate * 16000) / m_outputSampleRate;
    } else {
        samplesToReadFromBuffer = samplesToGenerate;
    }

    // Read the native 16kHz samples
    std::vector<float> nativeSamples(samplesToReadFromBuffer);
    m_jitterBuffer->readSamples(nativeSamples.data(), samplesToReadFromBuffer);

    // Resample them if needed
    std::vector<float>* finalSamples = &nativeSamples;
    std::vector<float> resampledSamples;
    if (m_outputResampler) {
        resampledSamples = m_outputResampler->process(nativeSamples.data(), nativeSamples.size());
        finalSamples = &resampledSamples;
    }

    // Write the resampled data to the buffer with format conversion if needed
    qint64 bytesToWrite = finalSamples->size() * bytesPerSample;
    
    if (m_sampleFormat == QAudioFormat::Int16) {
        // Convert float samples to Int16
        qint16* int16Data = reinterpret_cast<qint16*>(data);
        for (size_t i = 0; i < finalSamples->size(); ++i) {
            float sample = finalSamples->at(i);
            // Clamp to [-1.0, 1.0] and convert to Int16
            sample = std::max(-1.0f, std::min(1.0f, sample));
            int16Data[i] = static_cast<qint16>(sample * 32767.0f);
        }
    } else {
        // Float format - direct copy
        memcpy(data, finalSamples->data(), bytesToWrite);
    }

    // Return the ACTUAL number of bytes written. Do not lie.
    return bytesToWrite;
}

qint64 AudioStreamDevice::writeData(const char*, qint64)
{
    return -1;
}

qint64 AudioStreamDevice::bytesAvailable() const
{
    const int availableNativeSamples = m_jitterBuffer->samplesInBuffer();
    const int bytesPerSample = (m_sampleFormat == QAudioFormat::Int16) ? sizeof(qint16) : sizeof(float);

    if (m_outputResampler) {
        // The number of available bytes after resampling (16kHz -> output sample rate)
        const int resampledSamples = (availableNativeSamples * m_outputSampleRate) / 16000;
        return resampledSamples * bytesPerSample;
    } else {
        // No resampling, so it's a 1:1 mapping
        return availableNativeSamples * bytesPerSample;
    }
}

void AudioStreamDevice::triggerReadyRead()
{
    // Always trigger readyRead when we get new data
    // The readData method handles the case when there's insufficient data
    emit readyRead();
}

