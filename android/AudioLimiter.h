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

#ifndef AUDIOLIMITER_H
#define AUDIOLIMITER_H

#include <cmath>

// SVXLink-style audio limiter for FM transmission (-6dBFS)
// Fast attack (~2ms) / slow release (~20ms) envelope detector
// with 10:1 compression ratio
class AudioLimiter {
public:
    AudioLimiter() : thresholdDb_(-6.0), ratio_(0.1), outputGain_(1.0), envDb_(DC_OFFSET) {}

    void processAudio(float* samples, int count);

private:
    double thresholdDb_;  // -6dB threshold for FM
    double ratio_;        // 0.1 = 10:1 compression ratio
    double outputGain_;   // Output gain
    double envDb_;        // Envelope detector state

    static constexpr double ATTACK_COEF = 0.99;   // ~2ms attack
    static constexpr double RELEASE_COEF = 0.9995; // ~20ms release
    static constexpr double DC_OFFSET = 1.0E-25;

    inline double lin2dB(double lin) const {
        static const double LOG_2_DB = 8.6858896380650365530225783783321;
        return log(lin) * LOG_2_DB;
    }

    inline double dB2lin(double dB) const {
        static const double DB_2_LOG = 0.11512925464970228420089957273422;
        return exp(dB * DB_2_LOG);
    }
};

#endif // AUDIOLIMITER_H
