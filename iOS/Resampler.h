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

#ifndef RESAMPLER_H
#define RESAMPLER_H

#include <vector>

#include <deque>

class Resampler {
public:
    Resampler(int inRate, int outRate, int channels);
    ~Resampler();

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    std::vector<float> process(const float* input, int sampleCount);
    void reset();

private:
    enum Mode { Linear, Decim48To16, Interp16To48 } m_mode = Linear;

    int m_inRate;
    int m_outRate;
    int m_channels;

    // Linear mode state
    std::vector<float> m_prevSamples;
    double m_pos = 0.0;

    // FIR mode state
    std::deque<float> m_queue;
    std::vector<float> m_delay;
};

#endif // RESAMPLER_H
