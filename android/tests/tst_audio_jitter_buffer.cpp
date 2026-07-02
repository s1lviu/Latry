#include <QtTest>

#include "AudioJitterBuffer.h"

#include <array>

class AudioJitterBufferTest : public QObject
{
    Q_OBJECT

private slots:
    void prebufferBlocksPlaybackUntilThresholdIsReached();
    void underrunReentersPrebufferForShortGaps();
    void resizeAndPrebufferClampResetState();
    void overflowDropsTheOldestHalfOfBufferedSamples();
};

void AudioJitterBufferTest::prebufferBlocksPlaybackUntilThresholdIsReached()
{
    AudioJitterBuffer buffer(8);
    const std::array<float, 3> firstInput{1.0f, 2.0f, 3.0f};
    const std::array<float, 1> secondInput{4.0f};
    std::array<float, 4> output{9.0f, 9.0f, 9.0f, 9.0f};

    buffer.setPrebufSamples(4);
    buffer.writeSamples(firstInput.data(), static_cast<int>(firstInput.size()));

    QCOMPARE(buffer.samplesInBuffer(), 3u);
    QCOMPARE(buffer.samplesReadyForPlayback(), 0u);
    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 0);

    const std::array<float, 4> blockedOutput{0.0f, 0.0f, 0.0f, 0.0f};
    QVERIFY(output == blockedOutput);

    buffer.writeSamples(secondInput.data(), static_cast<int>(secondInput.size()));
    QCOMPARE(buffer.samplesReadyForPlayback(), 4u);
    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 4);

    const std::array<float, 4> expected{1.0f, 2.0f, 3.0f, 4.0f};
    QVERIFY(output == expected);
    QVERIFY(buffer.empty());
}

void AudioJitterBufferTest::underrunReentersPrebufferForShortGaps()
{
    AudioJitterBuffer buffer(8);
    const std::array<float, 4> initialBurst{1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<float, 2> shortRefill{5.0f, 6.0f};
    const std::array<float, 2> secondRefill{7.0f, 8.0f};
    std::array<float, 4> output{};

    buffer.setPrebufSamples(4);
    buffer.writeSamples(initialBurst.data(), static_cast<int>(initialBurst.size()));
    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 4);
    QVERIFY(buffer.empty());

    buffer.writeSamples(shortRefill.data(), static_cast<int>(shortRefill.size()));
    QCOMPARE(buffer.samplesReadyForPlayback(), 0u);
    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 0);

    const std::array<float, 4> blockedOutput{0.0f, 0.0f, 0.0f, 0.0f};
    QVERIFY(output == blockedOutput);

    buffer.writeSamples(secondRefill.data(), static_cast<int>(secondRefill.size()));
    QCOMPARE(buffer.samplesReadyForPlayback(), 4u);
    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 4);

    const std::array<float, 4> expected{5.0f, 6.0f, 7.0f, 8.0f};
    QVERIFY(output == expected);
}

void AudioJitterBufferTest::resizeAndPrebufferClampResetState()
{
    AudioJitterBuffer buffer(8);
    const std::array<float, 2> input{4.0f, 5.0f};
    std::array<float, 2> output{9.0f, 9.0f};

    buffer.writeSamples(input.data(), static_cast<int>(input.size()));
    buffer.setPrebufSamples(99);
    QCOMPARE(buffer.prebufSamples(), 7u);

    buffer.setSize(4);
    QCOMPARE(buffer.samplesInBuffer(), 0u);
    QCOMPARE(buffer.prebufSamples(), 3u);

    buffer.setPrebufSamples(99);
    QCOMPARE(buffer.prebufSamples(), 3u);

    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 0);
    const std::array<float, 2> expected{0.0f, 0.0f};
    QVERIFY(output == expected);
}

void AudioJitterBufferTest::overflowDropsTheOldestHalfOfBufferedSamples()
{
    AudioJitterBuffer buffer(6);
    const std::array<float, 6> input{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    std::array<float, 3> output{};

    buffer.writeSamples(input.data(), static_cast<int>(input.size()));

    QCOMPARE(buffer.samplesInBuffer(), 3u);

    QCOMPARE(buffer.readSamples(output.data(), static_cast<int>(output.size())), 3);

    const std::array<float, 3> expected{4.0f, 5.0f, 6.0f};
    QVERIFY(output == expected);
}

QTEST_APPLESS_MAIN(AudioJitterBufferTest)

#include "tst_audio_jitter_buffer.moc"
