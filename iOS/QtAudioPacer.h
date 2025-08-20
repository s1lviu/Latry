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

#ifndef QTAUDIOPACER_H
#define QTAUDIOPACER_H

#include <QObject>
#include <QIODevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QTimer>
#include <vector>

class QtAudioPacer : public QObject
{
    Q_OBJECT
public:
    QtAudioPacer(int sampleRate,           // sink sample-rate
                 int blockSamples,         // samples PER CHANNEL per 20 ms (320 @ 16 kHz, 960 @ 48 kHz…)
                 int prebufMs,             // how much to pre-fill before start
                 const QAudioFormat &fmt,  // sink format (so we know int16 vs float, #ch, etc.)
                 QIODevice *output,
                 QAudioSink *sink,
                 QObject *parent = nullptr);

    void writeFromNet(const float *samples, int count);  // mono input, −1…+1
    void flush();
    void setOutputDevice(QIODevice *output);
    void maintainAudioSink();

private slots:
    void outputNextBlock();

private:
    const int           m_blockSamples;      // per-channel
    int           m_prebufSamples;     // per-channel
    const int           m_channels;
    const bool          m_useInt16;
    const int           m_bytesPerSample;    // 2 or 4
    const int           m_blockBytes;        // whole frame = blockSamples * channels * bytes
    std::vector<float>  m_fifo;              // mono samples
    QIODevice          *m_out;
    QAudioSink         *m_sink;
    QTimer              m_timer;
};

#endif // QTAUDIOPACER_H
