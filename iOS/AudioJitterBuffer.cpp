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
    const unsigned head = m_head.load(std::memory_order_acquire);
    const unsigned tail = m_tail.load(std::memory_order_acquire);
    return (head + m_fifoSize - tail) % m_fifoSize;
}

void AudioJitterBuffer::clear()
{
    m_tail.store(0u, std::memory_order_relaxed);
    m_head.store(0u, std::memory_order_release);
    m_prebuf = (m_prebufSamples > 0);
}

void AudioJitterBuffer::writeSamples(const float* samples, int count)
{
    if (count <= 0) return;
    unsigned head = m_head.load(std::memory_order_relaxed);
    unsigned tail = m_tail.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        m_fifo[head] = samples[i];                    // write sample
        unsigned next = (head + 1) % m_fifoSize;
        if (next == tail) {                           // full after this write
            // Drop half of the buffer (your existing policy) to make room
            const unsigned drop = (m_fifoSize >> 1);
            tail = (tail + drop) % m_fifoSize;
            m_tail.store(tail, std::memory_order_release);
        }
        head = next;
        m_head.store(head, std::memory_order_release); // publish progress
    }
    // Prebuffering logic moved to AudioStreamDevice
}

void AudioJitterBuffer::readSamples(float* output, int count)
{
    if (count <= 0) return;

    unsigned head = m_head.load(std::memory_order_acquire);
    unsigned tail = m_tail.load(std::memory_order_relaxed);
    unsigned avail = (head + m_fifoSize - tail) % m_fifoSize;
    int i = 0;
    while (i < count && avail > 0) {
        output[i++] = m_fifo[tail];
        tail = (tail + 1) % m_fifoSize;
        --avail;
    }
    m_tail.store(tail, std::memory_order_release);    // publish consumption
    // Fill remaining with silence if buffer doesn't have enough data
    if (i < count) {
        std::fill(output + i, output + count, 0.0f);
    }
}
