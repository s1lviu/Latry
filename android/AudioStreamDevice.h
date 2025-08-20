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

#ifndef AUDIOSTREAMDEVICE_H
#define AUDIOSTREAMDEVICE_H

#include <QIODevice>
#include <QAudioFormat>
#include "AudioJitterBuffer.h"
#include "Resampler.h"

class AudioStreamDevice : public QIODevice
{
    Q_OBJECT
public:
    explicit AudioStreamDevice(AudioJitterBuffer* jitterBuffer, Resampler* resampler, int outputSampleRate, QAudioFormat::SampleFormat sampleFormat, QObject *parent = nullptr);
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;
    qint64 bytesAvailable() const override;

public slots:
    void triggerReadyRead();

private:
    AudioJitterBuffer* m_jitterBuffer;
    Resampler* m_outputResampler;
    int m_outputSampleRate;
    QAudioFormat::SampleFormat m_sampleFormat;
};

#endif // AUDIOSTREAMDEVICE_H