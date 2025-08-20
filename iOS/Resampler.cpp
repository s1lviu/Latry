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

#include "Resampler.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iterator>

// FIR filter coefficients taken from SvxLink 24.02
static const float coeff_48_16[] = {
    -0.0006552324784575, -0.0023665474931056, -0.0046009521986267,
    -0.0065673940075750, -0.0063452223170932, -0.0030442928485507,
    0.0027216740916904,  0.0079365191173948,  0.0088820372171036,
    0.0034577679862077, -0.0063356171066514, -0.0145569576678951,
    -0.0143873806232840, -0.0031353455170217, 0.0143500967202013,
    0.0267723137455069,  0.0227432656734411, -0.0007785303731755,
    -0.0333072891420923, -0.0533991698157678, -0.0390764894652067,
    0.0189267202445683,  0.1088868590088443,  0.2005613197280159,
    0.2583048205906900,  0.2583048205906900,  0.2005613197280159,
    0.1088868590088443,  0.0189267202445683, -0.0390764894652067,
    -0.0533991698157678, -0.0333072891420923, -0.0007785303731755,
    0.0227432656734411,  0.0267723137455069,  0.0143500967202013,
    -0.0031353455170217, -0.0143873806232840, -0.0145569576678951,
    -0.0063356171066514,  0.0034577679862077,  0.0088820372171036,
    0.0079365191173948,  0.0027216740916904, -0.0030442928485507,
    -0.0063452223170932, -0.0065673940075750, -0.0046009521986267,
    -0.0023665474931056, -0.0006552324784575
};

static const float coeff_48_16_wide[] = {
    5.11059239270262E-4, -8.255590813253409E-4, -0.0022883650051252883,
    -0.00291284164121095, -0.0012268298491091916, 0.0022762075309263855,
    0.004665122182146708, 0.0028373838432406684, -0.0029213363716820875,
    -0.007788031828919018, -0.006016833804341717, 0.002968009107977126,
    0.01198761593254768,  0.011232706838970668, -0.0019206055143741107,
    -0.017561483250559024, -0.019661897398973553, -0.0011813015957021255,
    0.025346590995928835, 0.034210485687661864, 0.008664040822720114,
    -0.03840386432673845, -0.0655288086799168, -0.030167800561122577,
    0.07566615695450109,  0.21042482376878066, 0.3043049697785759,
    0.3043049697785759,  0.21042482376878066, 0.07566615695450109,
    -0.030167800561122577, -0.0655288086799168, -0.03840386432673845,
    0.008664040822720114, 0.034210485687661864, 0.025346590995928835,
    -0.0011813015957021255, -0.019661897398973553, -0.017561483250559024,
    -0.0019206055143741107, 0.011232706838970668, 0.01198761593254768,
    0.002968009107977126, -0.006016833804341717, -0.007788031828919018,
    -0.0029213363716820875, 0.0028373838432406684, 0.004665122182146708,
    0.0022762075309263855, -0.0012268298491091916, -0.00291284164121095,
    -0.0022883650051252883, -8.255590813253409E-4, 5.11059239270262E-4
};

Resampler::Resampler(int inRate, int outRate, int channels)
    : m_inRate(inRate), m_outRate(outRate), m_channels(channels)
{
    if (inRate == 48000 && outRate == 16000) {
        if (channels != 1) {
            // FIR path is mono-only; fall back to linear to avoid corrupting interleaved data
            m_mode = Linear;
            m_prevSamples.resize(m_channels, 0.0f);
        } else {
            m_mode = Decim48To16;
            m_delay.assign(std::size(coeff_48_16_wide), 0.0f);
        }
    } else if (inRate == 16000 && outRate == 48000) {
        if (channels != 1) {
            m_mode = Linear;
            m_prevSamples.resize(m_channels, 0.0f);
        } else {
            m_mode = Interp16To48;
            m_delay.assign(std::size(coeff_48_16) / 3, 0.0f);
        }
    } else {
        m_mode = Linear;
        m_prevSamples.resize(m_channels, 0.0f);
    }
}

Resampler::~Resampler() {}

std::vector<float> Resampler::process(const float* input, int sampleCount)
{
    if (sampleCount <= 0)
        return {};

    std::vector<float> output;

    if (m_mode == Linear) {
        double step = static_cast<double>(m_inRate) / m_outRate;
        int estOut = static_cast<int>((sampleCount + 1) * (static_cast<double>(m_outRate) / m_inRate) + 2);
        std::vector<float> data(m_channels * (sampleCount + 1));
        for (int ch = 0; ch < m_channels; ++ch)
            data[ch] = m_prevSamples[ch];
        std::memcpy(data.data() + m_channels, input, sampleCount * m_channels * sizeof(float));

        output.reserve(estOut * m_channels);
        double pos = m_pos;
        int avail = sampleCount + 1;
        while (pos < avail - 1) {
            int idx = static_cast<int>(pos);
            double frac = pos - idx;
            for (int ch = 0; ch < m_channels; ++ch) {
                float s0 = data[idx * m_channels + ch];
                float s1 = data[(idx + 1) * m_channels + ch];
                output.push_back(s0 + (s1 - s0) * frac);
            }
            pos += step;
        }

        m_pos = pos - (avail - 1);
        for (int ch = 0; ch < m_channels; ++ch)
            m_prevSamples[ch] = data[(avail - 1) * m_channels + ch];
    } else if (m_mode == Decim48To16) {
        for (int i = 0; i < sampleCount; ++i)
            m_queue.push_back(input[i]);
        while (m_queue.size() >= 3) {
            for (size_t i = m_delay.size(); i-- > 3;)
                m_delay[i] = m_delay[i - 3];
            for (int i = 2; i >= 0; --i) {
                m_delay[i] = m_queue.front();
                m_queue.pop_front();
            }
            float sum = 0.0f;
            for (size_t t = 0; t < std::size(coeff_48_16_wide); ++t)
                sum += coeff_48_16_wide[t] * m_delay[t];
            output.push_back(sum);
        }
    } else if (m_mode == Interp16To48) {
        for (int i = 0; i < sampleCount; ++i)
            m_queue.push_back(input[i]);
        const size_t tapsPerPhase = std::size(coeff_48_16) / 3;
        while (!m_queue.empty()) {
            for (size_t i = tapsPerPhase; i-- > 1;)
                m_delay[i] = m_delay[i - 1];
            m_delay[0] = m_queue.front();
            m_queue.pop_front();

            for (int phase = 0; phase < 3; ++phase) {
                const float *coeff = coeff_48_16 + phase;
                float sum = 0.0f;
                for (size_t t = 0; t < tapsPerPhase; ++t)
                    sum += coeff[t * 3] * m_delay[t];
                output.push_back(sum * 3.0f);
            }
        }
    }

    return output;
}

void Resampler::reset()
{
    m_pos = 0.0;
    std::fill(m_prevSamples.begin(), m_prevSamples.end(), 0.0f);
    m_queue.clear();
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
}
