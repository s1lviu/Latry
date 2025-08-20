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

#include "AudioJitterBuffer.h"

AudioJitterBuffer::AudioJitterBuffer(unsigned fifoSize)
    : m_fifoSize(fifoSize), m_fifo(fifoSize)
{
}

void AudioJitterBuffer::setSize(unsigned newSize)
{
    m_fifoSize = newSize ? newSize : 1;
    m_fifo.assign(m_fifoSize, 0.0f);
    clear();
}

void AudioJitterBuffer::setPrebufSamples(unsigned prebufSamples)
{
    m_prebufSamples = std::min(prebufSamples, m_fifoSize - 1);
    if (empty())
        m_prebuf = (m_prebufSamples > 0);
}

unsigned AudioJitterBuffer::samplesInBuffer() const
{
    return (m_head + m_fifoSize - m_tail) % m_fifoSize;
}

void AudioJitterBuffer::clear()
{
    m_head = m_tail = 0;
    m_prebuf = (m_prebufSamples > 0);
}

void AudioJitterBuffer::writeSamples(const float* samples, int count)
{
    if (count <= 0) return;
    for (int i = 0; i < count; ++i) {
        m_fifo[m_head] = samples[i];
        m_head = (m_head + 1) % m_fifoSize;
        if (m_head == m_tail) {
            // Drop half of the buffered samples when full
            m_tail = (m_tail + (m_fifoSize >> 1)) % m_fifoSize;
        }
    }
    // Prebuffering logic moved to AudioStreamDevice
}

void AudioJitterBuffer::readSamples(float* output, int count)
{
    if (count <= 0) return;

    unsigned avail = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    int i = 0;
    while (i < count && avail > 0) {
        output[i++] = m_fifo[m_tail];
        m_tail = (m_tail + 1) % m_fifoSize;
        --avail;
    }
    // Fill remaining with silence if buffer doesn't have enough data
    if (i < count) {
        std::fill(output + i, output + count, 0.0f);
    }
}
