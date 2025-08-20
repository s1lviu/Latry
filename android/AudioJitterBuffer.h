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

#ifndef AUDIOJITTERBUFFER_H
#define AUDIOJITTERBUFFER_H

#include <vector>
#include <algorithm>
#include <cstring>

class AudioJitterBuffer
{
public:
    explicit AudioJitterBuffer(unsigned fifoSize = 3200);

    void setSize(unsigned newSize);
    void setPrebufSamples(unsigned prebufSamples);

    bool empty() const { return m_head == m_tail; }
    unsigned samplesInBuffer() const;
    unsigned prebufSamples() const { return m_prebufSamples; }

    void clear();
    void writeSamples(const float* samples, int count);
    void readSamples(float* output, int count);

private:
    std::vector<float> m_fifo;
    unsigned m_fifoSize;
    unsigned m_head = 0;
    unsigned m_tail = 0;
    unsigned m_prebufSamples = 0;
    bool m_prebuf = true;
};

#endif // AUDIOJITTERBUFFER_H
