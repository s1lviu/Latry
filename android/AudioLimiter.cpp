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

#include "AudioLimiter.h"
#include <cmath>

void AudioLimiter::processAudio(float* samples, int count) {
    for (int i = 0; i < count; ++i) {
        double rectified = fabs(samples[i]);  // Rectify input signal
        rectified += DC_OFFSET;               // Add DC offset to avoid log(0)
        double keyDb = lin2dB(rectified);     // Convert linear to dB

        // Calculate overdB (how much above threshold)
        double overDb = keyDb - thresholdDb_;
        if (overDb < 0.0) {
            overDb = 0.0;
        }

        overDb += DC_OFFSET;  // Add DC offset to avoid denormal

        // Envelope detector with attack/release
        if (overDb > envDb_) {
            envDb_ = overDb + ATTACK_COEF * (envDb_ - overDb);   // Fast attack
        } else {
            envDb_ = overDb + RELEASE_COEF * (envDb_ - overDb);  // Slow release
        }

        overDb = envDb_ - DC_OFFSET;  // Remove DC offset

        // Calculate gain reduction using compression ratio
        double gainReductionDb = overDb * (ratio_ - 1.0);  // Gain reduction in dB
        double gainReduction = dB2lin(gainReductionDb);    // Convert to linear

        // Apply gain reduction to input sample
        samples[i] = outputGain_ * samples[i] * gainReduction;
    }
}
