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

#ifndef OPUSWRAPPER_H
#define OPUSWRAPPER_H

#include <opus.h>
#include <vector>

class OpusEncoder {
public:
    OpusEncoder(opus_int32 sample_rate, int channels, int application);
    ~OpusEncoder();
    OpusEncoder(const OpusEncoder&) = delete;
    OpusEncoder& operator=(const OpusEncoder&) = delete;

    int encode(const float* pcm, int frame_size, unsigned char* output, int max_output_bytes);

    void applySvxlinkDefaults();

private:
    ::OpusEncoder* m_encoder = nullptr;
};

class OpusDecoder {
public:
    OpusDecoder(opus_int32 sample_rate, int channels);
    ~OpusDecoder();
    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    int decode(const unsigned char* data, int len, float* pcm, int frame_size);

private:
    ::OpusDecoder* m_decoder = nullptr;
    int m_channels           = 1;
};

#endif // OPUSWRAPPER_H
