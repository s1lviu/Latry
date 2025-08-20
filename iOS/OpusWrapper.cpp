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

// OpusWrapper.cpp
#include "OpusWrapper.h"
#include <QDebug>

// -----------------------------------------------------------------------------
// OpusEncoder  – unchanged
// -----------------------------------------------------------------------------
OpusEncoder::OpusEncoder(opus_int32 sample_rate,
                         int        channels,
                         int        application)
{
    int error = 0;
    m_encoder = opus_encoder_create(sample_rate, channels,
                                    application, &error);
    if (error != OPUS_OK) {
        qCritical() << "Failed to create Opus encoder:"
                    << opus_strerror(error);
        m_encoder = nullptr;
    }
}

OpusEncoder::~OpusEncoder()
{
    if (m_encoder)
        opus_encoder_destroy(m_encoder);
}

int OpusEncoder::encode(const float*       pcm,
                        int                frame_size,
                        unsigned char*     output,
                        int                max_output_bytes)
{
    if (!m_encoder)
        return -1;

    return opus_encode_float(m_encoder, pcm, frame_size,
                             output, max_output_bytes);
}

void OpusEncoder::applySvxlinkDefaults()
{
    if (!m_encoder) return;

    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(20000));
    opus_encoder_ctl(m_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(m_encoder, OPUS_SET_DTX(0));
#if OPUS_MAJOR > 0
    opus_encoder_ctl(m_encoder, OPUS_SET_LSB_DEPTH(16));
#endif

#if defined(Q_OS_IOS)
    // iOS-specific: Maximize input gain and disable automatic gain control
    // This prevents Opus from reducing input levels on iOS
    opus_encoder_ctl(m_encoder, OPUS_SET_FORCE_CHANNELS(1)); // Force mono
    // Disable any automatic level adjustment that might reduce iOS input
    qDebug() << "OpusEncoder: Applied iOS-specific settings for maximum input gain";
#endif
}

// -----------------------------------------------------------------------------
// OpusDecoder  – FIXED (scaling removed)
// -----------------------------------------------------------------------------
OpusDecoder::OpusDecoder(opus_int32 sample_rate, int channels)
    : m_channels(channels)
{
    int error = 0;
    m_decoder = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK) {
        qCritical() << "Failed to create Opus decoder:"
                    << opus_strerror(error);
        m_decoder = nullptr;
    }
}

OpusDecoder::~OpusDecoder()
{
    if (m_decoder)
        opus_decoder_destroy(m_decoder);
}

int OpusDecoder::decode(const unsigned char* data,
                        int                  len,
                        float*               pcm,
                        int                  frame_size)
{
    if (!m_decoder)
        return -1;

    /* opus_decode_float() already returns samples in [-1, +1]  */
    return opus_decode_float(m_decoder, data, len,
                             pcm, frame_size, 0);
}
