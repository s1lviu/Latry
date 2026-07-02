#include <QtTest>

#include "AudioLimiter.h"

#include <algorithm>
#include <array>
#include <cmath>

class AudioLimiterTest : public QObject
{
    Q_OBJECT

private slots:
    void silenceRemainsSilent();
    void belowThresholdSignalPassesThrough();
    void hotSignalIsAttenuated();
};

void AudioLimiterTest::silenceRemainsSilent()
{
    AudioLimiter limiter;
    std::array<float, 8> samples{};

    limiter.processAudio(samples.data(), static_cast<int>(samples.size()));

    const std::array<float, 8> expected{};
    QVERIFY(samples == expected);
}

void AudioLimiterTest::belowThresholdSignalPassesThrough()
{
    AudioLimiter limiter;
    std::array<float, 16> samples;
    samples.fill(0.25f);

    limiter.processAudio(samples.data(), static_cast<int>(samples.size()));

    for (float sample : samples)
        QVERIFY(std::fabs(sample - 0.25f) < 1.0e-6f);
}

void AudioLimiterTest::hotSignalIsAttenuated()
{
    AudioLimiter limiter;
    std::array<float, 256> samples;
    samples.fill(1.0f);

    limiter.processAudio(samples.data(), static_cast<int>(samples.size()));

    QVERIFY(std::all_of(samples.begin(), samples.end(), [](float sample) {
        return std::isfinite(sample) && sample <= 1.0f;
    }));
    QVERIFY(samples.back() < 0.7f);
}

QTEST_APPLESS_MAIN(AudioLimiterTest)

#include "tst_audio_limiter.moc"
