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

#include "QtAudioPacer.h"
#include <QByteArray>
#include <algorithm>
#include <QDebug>

QtAudioPacer::QtAudioPacer(int sr, int bs, int prebufMs,
                           const QAudioFormat &fmt,
                           QIODevice *out, QAudioSink *sink,
                           QObject *parent)
    : QObject(parent),
    m_blockSamples(bs),
    m_prebufSamples(prebufMs * sr / 1000),
    m_channels(fmt.channelCount()),
    m_useInt16(fmt.sampleFormat() == QAudioFormat::Int16),
    m_bytesPerSample(m_useInt16 ? sizeof(qint16) : sizeof(float)),
    m_blockBytes(bs * m_channels * m_bytesPerSample),
    m_out(out),
    m_sink(sink)
{
    m_fifo.reserve(m_prebufSamples * 2);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 * bs / sr);             // e.g. 20 ms
    connect(&m_timer, &QTimer::timeout,
            this, &QtAudioPacer::outputNextBlock);
}

void QtAudioPacer::writeFromNet(const float *s, int n)
{
    if (n <= 0) return;
    
    // Prevent FIFO from growing too large (more than 2 seconds of audio)
    const int maxFifoSize = m_prebufSamples * 4; // 4x prebuffer = ~1.6 seconds
    if (static_cast<int>(m_fifo.size()) > maxFifoSize) {
        // Drop oldest samples to make room
        int dropSamples = m_blockSamples * 2; // Drop 2 blocks worth
        m_fifo.erase(m_fifo.begin(), m_fifo.begin() + std::min(dropSamples, static_cast<int>(m_fifo.size())));
        qDebug() << "QtAudioPacer: FIFO overflow, dropped" << dropSamples << "samples";
    }
    
    m_fifo.insert(m_fifo.end(), s, s + n);
    if (!m_timer.isActive() && static_cast<int>(m_fifo.size()) >= m_prebufSamples)
        m_timer.start();
}

void QtAudioPacer::flush()
{
    m_fifo.clear();
    if (m_timer.isActive())
        m_timer.stop();
}

void QtAudioPacer::setOutputDevice(QIODevice *output)
{
    m_out = output;
}

void QtAudioPacer::outputNextBlock()
{
    if (!m_out || !m_sink) return;

    // Check if audio sink is suspended and resume it
    if (m_sink->state() == QAudio::SuspendedState) {
        qDebug() << "QtAudioPacer: Audio sink is suspended, resuming...";
        m_sink->resume();
        return;
    }

    // More lenient buffer check - allow writing if we have at least some space
    if (m_sink->bytesFree() < (m_blockBytes / 2)) {
        qWarning() << "QtAudioPacer: Buffer nearly full, skipping write. BytesFree:" << m_sink->bytesFree() << "BlockBytes:" << m_blockBytes;
        
        // Clear some FIFO data to prevent endless accumulation
        if (m_fifo.size() > m_prebufSamples * 3) {
            int dropSamples = m_blockSamples;
            m_fifo.erase(m_fifo.begin(), m_fifo.begin() + std::min(dropSamples, static_cast<int>(m_fifo.size())));
            qDebug() << "QtAudioPacer: Dropped" << dropSamples << "samples to prevent buffer overflow";
        }
        return;
    }

    static int underruns = 0;

    /* ---------------- collect a full block or pad with zeros -------- */
    QByteArray pcm;
    pcm.resize(m_blockBytes);

    if (static_cast<int>(m_fifo.size()) >= m_blockSamples) {
        /* enough real audio ------------------------------ */
        if (m_useInt16) {
            auto *dst = reinterpret_cast<qint16*>(pcm.data());
            for (int i = 0; i < m_blockSamples; ++i) {
                const qint16 v = static_cast<qint16>(
                    std::clamp(m_fifo[i], -1.f, 1.f) * 32767.f);
                for (int ch = 0; ch < m_channels; ++ch)
                    dst[i * m_channels + ch] = v;
            }
        } else {
            auto *dst = reinterpret_cast<float*>(pcm.data());
            for (int i = 0; i < m_blockSamples; ++i)
                for (int ch = 0; ch < m_channels; ++ch)
                    dst[i * m_channels + ch] = m_fifo[i];
        }
        m_fifo.erase(m_fifo.begin(), m_fifo.begin() + m_blockSamples);
        underruns = 0; // Reset underrun counter when we have real audio
    } else {
        /* underrun – fill the block with silence ---------- */
        pcm.fill(0);
        ++underruns;
        if (underruns == 25) {             // ~½ s of glitches
            // grow safety margin by one frame
            m_prebufSamples += m_blockSamples;
            underruns = 0;
        }
    }

    // Always write data to keep audio sink active
    qint64 bytesWritten = m_out->write(pcm);
    if (bytesWritten != m_blockBytes) {
        qWarning() << "QtAudioPacer: Only wrote" << bytesWritten << "of" << m_blockBytes << "bytes";
    }
}

void QtAudioPacer::maintainAudioSink()
{
    if (!m_sink || !m_out) return;
    
    QAudio::State state = m_sink->state();
    
    switch (state) {
    case QAudio::StoppedState:
        qDebug() << "QtAudioPacer: Audio sink is stopped, restarting...";
        m_out = m_sink->start();
        break;
    case QAudio::SuspendedState:
        qDebug() << "QtAudioPacer: Audio sink is suspended, resuming...";
        m_sink->resume();
        break;
    case QAudio::IdleState:
        // IdleState is normal when no audio is playing, but we want to keep it active
        // Write a small amount of silence to keep it active
        if (m_sink->bytesFree() >= m_blockBytes) {
            QByteArray silence(m_blockBytes, 0);
            m_out->write(silence);
        }
        break;
    case QAudio::ActiveState:
        // Normal state, nothing to do
        break;
    default:
        break;
    }
}
