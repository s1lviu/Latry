#include <QtTest>

#include "AudioEngine.h"

#include <array>
#include <limits>
#include <thread>
#include <vector>

#include <opus.h>

class AudioEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initializeAudioComponentsUsesVoipEncoderApplication();
    void startupPrimingBuffersUntilTargetThenEncodesFrames();
    void startupLeadInEmitsTwoSilentFrames();
    void flushPendingSamplesPadsPartialFrame();
    void processCapturedInt16SamplesEncodesAtNativeRate();
    void queuedNativeFloatCaptureEncodesViaEventLoop();
    void queuedNativeFloatCaptureDropsAfterStop();
    void processReceivedAudioDropsStaleOutOfOrderPackets();
    void processReceivedAudioCapsPacketLossConcealmentFrames();
    void processReceivedAudioTreatsSequenceZeroAsRealPacket();
    void processReceivedAudioHandlesSequenceWraparound();
    void txGainLevelIsClampedAndApplied();

private:
    void configureEncoder(AudioEngine &engine);
    QByteArray encodeFramePacket(float sampleValue = 0.2f);
};

void AudioEngineTest::configureEncoder(AudioEngine &engine)
{
    engine.m_encoder = std::make_unique<OpusEncoder>(
        AudioEngine::SAMPLE_RATE,
        AudioEngine::CHANNELS,
        OPUS_APPLICATION_VOIP);
    QVERIFY(engine.m_encoder != nullptr);
    engine.m_encoder->applySvxlinkDefaults();
}

QByteArray AudioEngineTest::encodeFramePacket(float sampleValue)
{
    OpusEncoder encoder(AudioEngine::SAMPLE_RATE,
                        AudioEngine::CHANNELS,
                        OPUS_APPLICATION_VOIP);
    if (encoder.m_encoder == nullptr) {
        return {};
    }

    encoder.applySvxlinkDefaults();

    std::vector<float> samples(AudioEngine::FRAME_SIZE_SAMPLES, sampleValue);
    std::vector<unsigned char> buffer(4000);
    const int encodedSize = encoder.encode(samples.data(),
                                           AudioEngine::FRAME_SIZE_SAMPLES,
                                           buffer.data(),
                                           static_cast<int>(buffer.size()));
    if (encodedSize <= 0) {
        return {};
    }

    return QByteArray(reinterpret_cast<const char*>(buffer.data()), encodedSize);
}

void AudioEngineTest::initializeAudioComponentsUsesVoipEncoderApplication()
{
    AudioEngine engine;

    engine.initializeAudioComponents();

    QVERIFY(engine.m_encoder != nullptr);

    int application = 0;
    QCOMPARE(opus_encoder_ctl(engine.m_encoder->m_encoder, OPUS_GET_APPLICATION(&application)), OPUS_OK);
    QCOMPARE(application, OPUS_APPLICATION_VOIP);
    QCOMPARE(engine.m_jitterBuffer.prebufSamples(),
             static_cast<unsigned>(AudioEngine::FRAME_SIZE_SAMPLES * (150 / AudioEngine::FRAME_SIZE_MS)));
}

void AudioEngineTest::startupPrimingBuffersUntilTargetThenEncodesFrames()
{
    AudioEngine engine;
    configureEncoder(engine);
    engine.m_recording = true;

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);

    engine.prepareTxStartupPriming();

    std::vector<float> samples(AudioEngine::FRAME_SIZE_SAMPLES, 0.25f);
    engine.processCapturedFloatSamples(samples.data(), static_cast<int>(samples.size()));

    QCOMPARE(encodedSpy.count(), 0);
    QCOMPARE(engine.m_txStartupBuffer.size(), samples.size());
    QVERIFY(engine.m_txStartupPrimingActive);

    engine.processCapturedFloatSamples(samples.data(), static_cast<int>(samples.size()));

    QCOMPARE(encodedSpy.count(), 2);
    QVERIFY(!engine.m_txStartupPrimingActive);
    QVERIFY(engine.m_txStartupBuffer.empty());
    QVERIFY(engine.m_pendingInputSamples.empty());
}

void AudioEngineTest::startupLeadInEmitsTwoSilentFrames()
{
    AudioEngine engine;
    configureEncoder(engine);

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);

    engine.prepareTxStartupPriming();
    engine.sendTxStartupLeadIn();

    QCOMPARE(encodedSpy.count(), 2);
    for (int i = 0; i < encodedSpy.count(); ++i)
        QVERIFY(!encodedSpy.at(i).at(0).toByteArray().isEmpty());
}

void AudioEngineTest::flushPendingSamplesPadsPartialFrame()
{
    AudioEngine engine;
    configureEncoder(engine);

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);

    engine.m_pendingInputSamples.assign(AudioEngine::FRAME_SIZE_SAMPLES / 2, 0.1f);
    engine.flushPendingTxSamples();

    QCOMPARE(encodedSpy.count(), 1);
    QVERIFY(engine.m_pendingInputSamples.empty());
}

void AudioEngineTest::processCapturedInt16SamplesEncodesAtNativeRate()
{
    AudioEngine engine;
    configureEncoder(engine);
    engine.m_recording = true;

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);

    std::vector<short> samples(AudioEngine::FRAME_SIZE_SAMPLES, 4096);
    engine.processCapturedInt16Samples(samples.data(),
                                       static_cast<int>(samples.size()),
                                       AudioEngine::SAMPLE_RATE);

    QCOMPARE(encodedSpy.count(), 1);
}

void AudioEngineTest::queuedNativeFloatCaptureEncodesViaEventLoop()
{
    AudioEngine engine;
    configureEncoder(engine);
    engine.m_recording = true;

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);
    std::vector<float> samples(AudioEngine::FRAME_SIZE_SAMPLES, 0.25f);

    std::thread worker([&engine, &samples]() {
        engine.queueCapturedNativeFloatSamples(samples.data(),
                                               static_cast<int>(samples.size()),
                                               AudioEngine::SAMPLE_RATE);
    });
    worker.join();

    QTRY_COMPARE(encodedSpy.count(), 1);
    QVERIFY(engine.m_pendingInputSamples.empty());
}

void AudioEngineTest::queuedNativeFloatCaptureDropsAfterStop()
{
    AudioEngine engine;
    configureEncoder(engine);
    engine.m_recording = true;

    QSignalSpy encodedSpy(&engine, &AudioEngine::audioDataEncoded);
    std::vector<float> samples(AudioEngine::FRAME_SIZE_SAMPLES, 0.25f);
    bool barrierReached = false;

    std::thread worker([&engine, &samples]() {
        engine.queueCapturedNativeFloatSamples(samples.data(),
                                               static_cast<int>(samples.size()),
                                               AudioEngine::SAMPLE_RATE);
    });
    worker.join();

    engine.m_recording = false;
    engine.m_encoder.reset();

    QMetaObject::invokeMethod(&engine, [&barrierReached]() {
        barrierReached = true;
    }, Qt::QueuedConnection);

    QTRY_VERIFY(barrierReached);
    QCOMPARE(encodedSpy.count(), 0);
    QVERIFY(engine.m_pendingInputSamples.empty());
}

void AudioEngineTest::processReceivedAudioDropsStaleOutOfOrderPackets()
{
    AudioEngine engine;
    engine.m_audioReady = true;
    engine.m_decoder = std::make_unique<OpusDecoder>(AudioEngine::SAMPLE_RATE, AudioEngine::CHANNELS);

    const QByteArray packet = encodeFramePacket();
    QVERIFY(!packet.isEmpty());

    engine.processReceivedAudio(packet, 100);

    const unsigned bufferedSamples = engine.m_jitterBuffer.samplesInBuffer();
    QCOMPARE(bufferedSamples, static_cast<unsigned>(AudioEngine::FRAME_SIZE_SAMPLES));
    QCOMPARE(engine.m_lastAudioSeq, quint16(100));

    engine.processReceivedAudio(packet, 99);

    QCOMPARE(engine.m_jitterBuffer.samplesInBuffer(), bufferedSamples);
    QCOMPARE(engine.m_lastAudioSeq, quint16(100));
}

void AudioEngineTest::processReceivedAudioCapsPacketLossConcealmentFrames()
{
    AudioEngine threeFrameGapEngine;
    threeFrameGapEngine.m_audioReady = true;
    threeFrameGapEngine.m_decoder = std::make_unique<OpusDecoder>(AudioEngine::SAMPLE_RATE,
                                                                  AudioEngine::CHANNELS);

    AudioEngine fourFrameGapEngine;
    fourFrameGapEngine.m_audioReady = true;
    fourFrameGapEngine.m_decoder = std::make_unique<OpusDecoder>(AudioEngine::SAMPLE_RATE,
                                                                 AudioEngine::CHANNELS);

    const QByteArray packet = encodeFramePacket();
    QVERIFY(!packet.isEmpty());

    threeFrameGapEngine.processReceivedAudio(packet, 200);
    threeFrameGapEngine.processReceivedAudio(packet, 204);

    fourFrameGapEngine.processReceivedAudio(packet, 200);
    fourFrameGapEngine.processReceivedAudio(packet, 205);

    QCOMPARE(threeFrameGapEngine.m_lastAudioSeq, quint16(204));
    QCOMPARE(fourFrameGapEngine.m_lastAudioSeq, quint16(205));
    QCOMPARE(fourFrameGapEngine.m_jitterBuffer.samplesInBuffer(),
             threeFrameGapEngine.m_jitterBuffer.samplesInBuffer());
}

void AudioEngineTest::processReceivedAudioTreatsSequenceZeroAsRealPacket()
{
    AudioEngine engine;
    engine.m_audioReady = true;
    engine.m_decoder = std::make_unique<OpusDecoder>(AudioEngine::SAMPLE_RATE, AudioEngine::CHANNELS);

    const QByteArray packet = encodeFramePacket();
    QVERIFY(!packet.isEmpty());

    engine.processReceivedAudio(packet, 0);
    QCOMPARE(engine.m_hasLastAudioSeq, true);
    QCOMPARE(engine.m_lastAudioSeq, quint16(0));
    QCOMPARE(engine.m_jitterBuffer.samplesInBuffer(),
             static_cast<unsigned>(AudioEngine::FRAME_SIZE_SAMPLES));

    engine.processReceivedAudio(packet, 2);

    QCOMPARE(engine.m_lastAudioSeq, quint16(2));
    QCOMPARE(engine.m_jitterBuffer.samplesInBuffer(),
             static_cast<unsigned>(AudioEngine::FRAME_SIZE_SAMPLES * 3));
}

void AudioEngineTest::processReceivedAudioHandlesSequenceWraparound()
{
    AudioEngine engine;
    engine.m_audioReady = true;
    engine.m_decoder = std::make_unique<OpusDecoder>(AudioEngine::SAMPLE_RATE, AudioEngine::CHANNELS);

    const QByteArray packet = encodeFramePacket();
    QVERIFY(!packet.isEmpty());

    engine.processReceivedAudio(packet, std::numeric_limits<quint16>::max());
    engine.processReceivedAudio(packet, 0);

    QCOMPARE(engine.m_lastAudioSeq, quint16(0));
    QCOMPARE(engine.m_jitterBuffer.samplesInBuffer(),
             static_cast<unsigned>(AudioEngine::FRAME_SIZE_SAMPLES * 2));
}

void AudioEngineTest::txGainLevelIsClampedAndApplied()
{
    AudioEngine engine;
    std::array<float, 2> samples{0.1f, -0.1f};

    engine.setTxAudioLevelDb(50.0f);

    QCOMPARE(engine.m_txAudioLevelDb, 12.0f);
    QVERIFY(engine.m_txGainMultiplier > 3.9f);

    engine.applyTxGain(samples.data(), static_cast<int>(samples.size()));

    QVERIFY(samples[0] > 0.39f);
    QVERIFY(samples[1] < -0.39f);
}

QTEST_GUILESS_MAIN(AudioEngineTest)

#include "tst_audio_engine.moc"
