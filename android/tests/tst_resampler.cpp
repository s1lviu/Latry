#include <QtTest>

#include "Resampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
constexpr float kEpsilon = 1.0e-5f;

void verifyVectorClose(const std::vector<float>& actual,
                       const std::vector<float>& expected,
                       float epsilon = kEpsilon)
{
    QCOMPARE(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        QVERIFY2(std::fabs(actual[i] - expected[i]) <= epsilon,
                 qPrintable(QStringLiteral("Mismatch at index %1: actual=%2 expected=%3")
                                .arg(i)
                                .arg(actual[i], 0, 'f', 6)
                                .arg(expected[i], 0, 'f', 6)));
    }
}
} // namespace

class ResamplerTest : public QObject
{
    Q_OBJECT

private slots:
    void linearModeInterpolatesPredictably();
    void resetRestoresLinearModeState();
    void specializedModesProduceExpectedFrameCounts();
};

void ResamplerTest::linearModeInterpolatesPredictably()
{
    Resampler resampler(2, 4, 1);
    const std::array<float, 2> input{2.0f, 4.0f};

    const auto output = resampler.process(input.data(), static_cast<int>(input.size()));

    verifyVectorClose(output, {0.0f, 1.0f, 2.0f, 3.0f});
}

void ResamplerTest::resetRestoresLinearModeState()
{
    Resampler resampler(2, 4, 1);
    const std::array<float, 2> input{2.0f, 4.0f};

    const auto firstPass = resampler.process(input.data(), static_cast<int>(input.size()));
    const auto secondPass = resampler.process(input.data(), static_cast<int>(input.size()));

    QVERIFY(firstPass != secondPass);

    resampler.reset();

    const auto afterReset = resampler.process(input.data(), static_cast<int>(input.size()));
    verifyVectorClose(afterReset, firstPass);
}

void ResamplerTest::specializedModesProduceExpectedFrameCounts()
{
    Resampler decimator(48000, 16000, 1);
    const std::array<float, 6> decimationInput{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    const auto decimated = decimator.process(decimationInput.data(), static_cast<int>(decimationInput.size()));
    QCOMPARE(decimated.size(), size_t(2));
    QVERIFY(std::all_of(decimated.begin(), decimated.end(), [](float sample) {
        return std::isfinite(sample);
    }));

    decimator.reset();
    const auto decimatedAfterReset = decimator.process(decimationInput.data(), static_cast<int>(decimationInput.size()));
    verifyVectorClose(decimatedAfterReset, decimated);

    Resampler interpolator(16000, 48000, 1);
    const std::array<float, 3> interpolationInput{1.0f, 0.0f, -1.0f};

    const auto interpolated = interpolator.process(interpolationInput.data(), static_cast<int>(interpolationInput.size()));
    QCOMPARE(interpolated.size(), size_t(9));
    QVERIFY(std::all_of(interpolated.begin(), interpolated.end(), [](float sample) {
        return std::isfinite(sample);
    }));

    interpolator.reset();
    const auto interpolatedAfterReset = interpolator.process(interpolationInput.data(), static_cast<int>(interpolationInput.size()));
    verifyVectorClose(interpolatedAfterReset, interpolated);
}

QTEST_APPLESS_MAIN(ResamplerTest)

#include "tst_resampler.moc"
