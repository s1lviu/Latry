#ifndef ANDROIDAUDIOTRACKOUTPUT_H
#define ANDROIDAUDIOTRACKOUTPUT_H

#include <QString>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

class AudioJitterBuffer;

class AndroidAudioTrackOutput
{
public:
    explicit AndroidAudioTrackOutput(AudioJitterBuffer* jitterBuffer);
    ~AndroidAudioTrackOutput();

    bool start();
    void stop();
    void pause();
    void resume();
    bool isActive() const;
    bool applyCurrentRoute();

private:
    void playbackLoop();
    int writeSamplesBlocking(const float* samples, int count);
    bool ensureSampleArrayCapacity(int sampleCount, bool useFloat);
    void releaseSampleArray();
    QString currentRoute() const;

    AudioJitterBuffer* m_jitterBuffer = nullptr;
    mutable std::mutex m_stateMutex;
    std::condition_variable m_stateCondition;
    std::thread m_playbackThread;
    bool m_running = false;
    bool m_paused = false;
    bool m_stopRequested = false;
    bool m_useFloatPlayback = false;
    std::vector<short> m_pcm16Buffer;
    void* m_sampleArrayGlobal = nullptr;
    int m_sampleArraySize = 0;
    bool m_sampleArrayIsFloat = false;
};

#endif // ANDROIDAUDIOTRACKOUTPUT_H
