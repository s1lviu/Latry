#ifndef ANDROIDAUDIORECORDINPUT_H
#define ANDROIDAUDIORECORDINPUT_H

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class AndroidAudioRecordInput
{
public:
    using Int16SampleHandler = std::function<void(const short* samples, int count, int sampleRate)>;
    using FloatSampleHandler = std::function<void(float* samples, int count, int sampleRate)>;

    AndroidAudioRecordInput(Int16SampleHandler int16SampleHandler,
                            FloatSampleHandler floatSampleHandler);
    ~AndroidAudioRecordInput();

    bool prepare();
    bool start();
    void stop();
    void release();
    bool isCapturing() const;
    int sampleRate() const;
    bool usesFloatCapture() const;

private:
    void captureLoop();
    bool ensureSampleArrayCapacity(int sampleCount, bool useFloatSamples);
    void releaseSampleArray();

    Int16SampleHandler m_int16SampleHandler;
    FloatSampleHandler m_floatSampleHandler;
    mutable std::mutex m_stateMutex;
    std::thread m_captureThread;
    bool m_capturing = false;
    bool m_stopRequested = false;
    int m_sampleRate = 16000;
    bool m_useFloatCapture = false;
    std::vector<short> m_pcm16Buffer;
    std::vector<float> m_floatBuffer;
    void* m_sampleArrayGlobal = nullptr;
    int m_sampleArraySize = 0;
    bool m_sampleArrayUsesFloat = false;
};

#endif // ANDROIDAUDIORECORDINPUT_H
