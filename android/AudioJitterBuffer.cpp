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

namespace {
constexpr auto kShortGapRebufferWindow = std::chrono::milliseconds(100);
}

AudioJitterBuffer::AudioJitterBuffer(unsigned fifoSize)
    : m_fifoSize(fifoSize), m_fifo(fifoSize)
{
}

void AudioJitterBuffer::setSize(unsigned newSize)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fifoSize = newSize ? newSize : 1;
    m_prebufSamples = std::min(m_prebufSamples, m_fifoSize - 1);
    m_fifo.assign(m_fifoSize, 0.0f);
    m_head = m_tail = 0;
    m_prebuf = (m_prebufSamples > 0);
    m_lastWriteTime = std::chrono::steady_clock::time_point{};
}

void AudioJitterBuffer::setPrebufSamples(unsigned prebufSamples)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_prebufSamples = std::min(prebufSamples, m_fifoSize - 1);
    const unsigned available = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    if (available == 0) {
        m_prebuf = (m_prebufSamples > 0);
    } else if (m_prebuf && available >= m_prebufSamples) {
        m_prebuf = false;
    }
}

unsigned AudioJitterBuffer::samplesInBuffer() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_head + m_fifoSize - m_tail) % m_fifoSize;
}

unsigned AudioJitterBuffer::samplesReadyForPlayback() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const unsigned available = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    if (m_prebuf && available < m_prebufSamples) {
        return 0;
    }
    return available;
}

void AudioJitterBuffer::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_head = m_tail = 0;
    m_prebuf = (m_prebufSamples > 0);
    m_lastWriteTime = std::chrono::steady_clock::time_point{};
}

void AudioJitterBuffer::writeSamples(const float* samples, int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (count <= 0) return;
    for (int i = 0; i < count; ++i) {
        m_fifo[m_head] = samples[i];
        m_head = (m_head + 1) % m_fifoSize;
        if (m_head == m_tail) {
            // Drop half of the buffered samples when full
            m_tail = (m_tail + (m_fifoSize >> 1)) % m_fifoSize;
            m_prebuf = false;
        }
    }
    m_lastWriteTime = std::chrono::steady_clock::now();

    const unsigned available = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    if (m_prebuf && available >= m_prebufSamples) {
        m_prebuf = false;
    }
}

int AudioJitterBuffer::readSamples(float* output, int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (count <= 0) {
        return 0;
    }

    unsigned available = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    if (m_prebuf && available < m_prebufSamples) {
        if (output != nullptr) {
            std::fill(output, output + count, 0.0f);
        }
        return 0;
    }

    if (m_prebuf && available >= m_prebufSamples) {
        m_prebuf = false;
    }

    const int readCount = std::min(count, static_cast<int>(available));
    for (int i = 0; i < readCount; ++i) {
        output[i] = m_fifo[m_tail];
        m_tail = (m_tail + 1) % m_fifoSize;
    }

    if (output != nullptr && readCount < count) {
        std::fill(output + readCount, output + count, 0.0f);
    }

    const unsigned remaining = (m_head + m_fifoSize - m_tail) % m_fifoSize;
    if (remaining == 0 && m_prebufSamples > 0 && !m_prebuf && m_lastWriteTime != std::chrono::steady_clock::time_point{}) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastWriteTime <= kShortGapRebufferWindow) {
            m_prebuf = true;
        }
    }
    return readCount;
}
