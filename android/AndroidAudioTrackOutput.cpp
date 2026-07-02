#include "AndroidAudioTrackOutput.h"

#include "AudioEngine.h"
#include "AudioJitterBuffer.h"

#include <algorithm>
#include <chrono>
#include <limits>

#include <QDebug>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniEnvironment>
#  include <QtCore/QJniObject>

namespace {
constexpr char kQtNativeClass[] = "org/qtproject/qt/android/QtNative";
constexpr char kRouteManagerClass[] = "yo6say/latry/LatryAudioRouteManager";
constexpr char kAudioTrackPlayerClassName[] = "yo6say.latry.LatryAudioTrackPlayer";
constexpr char kSpeakerRoute[] = "speaker";

QJniObject androidContext()
{
    return QJniObject::callStaticObjectMethod(
        kQtNativeClass,
        "getContext",
        "()Landroid/content/Context;");
}

jclass audioTrackPlayerClass(QJniEnvironment& env)
{
    static jclass globalClass = nullptr;
    if (globalClass != nullptr) {
        return globalClass;
    }

    if (!env.isValid()) {
        qWarning() << "AndroidAudioTrackOutput: JNI environment is not valid for class resolution";
        return nullptr;
    }

    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioTrackOutput: Failed to get Android context for class resolution";
        return nullptr;
    }

    const QJniObject classLoader = context.callObjectMethod(
        "getClassLoader",
        "()Ljava/lang/ClassLoader;");
    if (!classLoader.isValid()) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to get application ClassLoader";
        return nullptr;
    }

    const QJniObject className = QJniObject::fromString(QString::fromLatin1(kAudioTrackPlayerClassName));
    const QJniObject classObject = classLoader.callObjectMethod(
        "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        className.object());
    if (!classObject.isValid()) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to load" << kAudioTrackPlayerClassName
                   << "through the application ClassLoader";
        return nullptr;
    }

    jclass localClass = static_cast<jclass>(classObject.object());
    if (localClass == nullptr) {
        env.checkAndClearExceptions();
        return nullptr;
    }

    globalClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    if (globalClass == nullptr) {
        qWarning() << "AndroidAudioTrackOutput: Failed to create global class reference";
    }
    return globalClass;
}

jmethodID writePcm16Method(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "writePcm16", "([SI)I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve writePcm16()";
    }
    return methodId;
}

jmethodID startPlaybackMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(
        klass,
        "startPlayback",
        "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve startPlayback()";
    }
    return methodId;
}

jmethodID stopPlaybackMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "stopPlayback", "()V");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve stopPlayback()";
    }
    return methodId;
}

jmethodID pausePlaybackMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "pausePlayback", "()V");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve pausePlayback()";
    }
    return methodId;
}

jmethodID resumePlaybackMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "resumePlayback", "()V");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve resumePlayback()";
    }
    return methodId;
}

jmethodID writePcmFloatMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "writePcmFloat", "([FI)I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve writePcmFloat()";
    }
    return methodId;
}

jmethodID getEncodingMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "getEncoding", "()I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve getEncoding()";
    }
    return methodId;
}

jmethodID setPlaybackRouteMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(
        klass,
        "setPlaybackRoute",
        "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve setPlaybackRoute()";
    }
    return methodId;
}

constexpr jint kEncodingPcmFloat = 4;
}
#endif

AndroidAudioTrackOutput::AndroidAudioTrackOutput(AudioJitterBuffer* jitterBuffer)
    : m_jitterBuffer(jitterBuffer)
{
}

AndroidAudioTrackOutput::~AndroidAudioTrackOutput()
{
    stop();
}

bool AndroidAudioTrackOutput::start()
{
#if defined(Q_OS_ANDROID)
    stop();

    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioTrackOutput: Failed to get Android context";
        return false;
    }

    QJniEnvironment env;
    jclass klass = audioTrackPlayerClass(env);
    jmethodID startMethodId = startPlaybackMethod(env);
    jmethodID stopMethodId = stopPlaybackMethod(env);
    jmethodID writeMethodId = writePcm16Method(env);
    if (klass == nullptr || startMethodId == nullptr || stopMethodId == nullptr || writeMethodId == nullptr) {
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve LatryAudioTrackPlayer JNI bindings";
        return false;
    }

    const QString route = currentRoute();
    const QJniObject routeString = QJniObject::fromString(route);
    const jboolean started = env->CallStaticBooleanMethod(
        klass,
        startMethodId,
        context.object(),
        routeString.object());
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: startPlayback() threw an exception";
        return false;
    }

    if (!started) {
        qWarning() << "AndroidAudioTrackOutput: startPlayback() failed";
        return false;
    }

    bool useFloat = false;
    jmethodID encodingMethodId = getEncodingMethod(env);
    if (encodingMethodId != nullptr) {
        const jint encoding = env->CallStaticIntMethod(klass, encodingMethodId);
        if (!env.checkAndClearExceptions()) {
            useFloat = (encoding == kEncodingPcmFloat);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_running = true;
        m_paused = false;
        m_stopRequested = false;
        m_useFloatPlayback = useFloat;
    }

    qDebug() << "AndroidAudioTrackOutput: playback encoding =" << (useFloat ? "FLOAT" : "INT16");
    m_playbackThread = std::thread(&AndroidAudioTrackOutput::playbackLoop, this);
    return true;
#else
    return false;
#endif
}

void AndroidAudioTrackOutput::stop()
{
#if defined(Q_OS_ANDROID)
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_stopRequested = true;
        m_paused = false;
    }
    m_stateCondition.notify_all();

    if (m_playbackThread.joinable()) {
        m_playbackThread.join();
    }

    QJniEnvironment env;
    jclass klass = audioTrackPlayerClass(env);
    jmethodID stopMethodId = stopPlaybackMethod(env);
    if (klass != nullptr && stopMethodId != nullptr) {
        env->CallStaticVoidMethod(klass, stopMethodId);
    }
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: stopPlayback() threw an exception";
    }

    releaseSampleArray();

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_running = false;
    m_paused = false;
    m_stopRequested = false;
#endif
}

void AndroidAudioTrackOutput::pause()
{
#if defined(Q_OS_ANDROID)
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running || m_paused) {
            return;
        }
        m_paused = true;
    }

    QJniEnvironment env;
    jclass klass = audioTrackPlayerClass(env);
    jmethodID pauseMethodId = pausePlaybackMethod(env);
    if (klass != nullptr && pauseMethodId != nullptr) {
        env->CallStaticVoidMethod(klass, pauseMethodId);
    }
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: pausePlayback() threw an exception";
    }
#endif
}

void AndroidAudioTrackOutput::resume()
{
#if defined(Q_OS_ANDROID)
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_running) {
            return;
        }
        m_paused = false;
    }

    QJniEnvironment env;
    jclass klass = audioTrackPlayerClass(env);
    jmethodID resumeMethodId = resumePlaybackMethod(env);
    if (klass != nullptr && resumeMethodId != nullptr) {
        env->CallStaticVoidMethod(klass, resumeMethodId);
    }
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: resumePlayback() threw an exception";
    }

    m_stateCondition.notify_all();
#endif
}

bool AndroidAudioTrackOutput::isActive() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_running;
}

bool AndroidAudioTrackOutput::applyCurrentRoute()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioTrackOutput: Failed to get Android context for route update";
        return false;
    }

    const QJniObject routeString = QJniObject::fromString(currentRoute());
    QJniEnvironment env;
    jclass klass = audioTrackPlayerClass(env);
    jmethodID setRouteMethodId = setPlaybackRouteMethod(env);
    if (klass == nullptr || setRouteMethodId == nullptr) {
        qWarning() << "AndroidAudioTrackOutput: Failed to resolve setPlaybackRoute()";
        return false;
    }

    const jboolean updated = env->CallStaticBooleanMethod(
        klass,
        setRouteMethodId,
        context.object(),
        routeString.object());
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: setPlaybackRoute() threw an exception";
        return false;
    }

    return updated;
#else
    return false;
#endif
}

void AndroidAudioTrackOutput::playbackLoop()
{
#if defined(Q_OS_ANDROID)
    QJniEnvironment env;
    if (!env.isValid()) {
        qWarning() << "AndroidAudioTrackOutput: Failed to attach playback thread to JVM";
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_running = false;
        return;
    }

    // Set real-time audio thread priority (THREAD_PRIORITY_URGENT_AUDIO = -19)
    {
        jclass processClass = env->FindClass("android/os/Process");
        if (processClass != nullptr) {
            jmethodID setThreadPriority = env->GetStaticMethodID(processClass, "setThreadPriority", "(I)V");
            if (setThreadPriority != nullptr) {
                env->CallStaticVoidMethod(processClass, setThreadPriority, static_cast<jint>(-19));
                if (env.checkAndClearExceptions()) {
                    qWarning() << "AndroidAudioTrackOutput: setThreadPriority(-19) threw an exception";
                }
            }
            env->DeleteLocalRef(processClass);
        } else {
            env.checkAndClearExceptions();
        }
    }

    std::vector<float> frame(AudioEngine::FRAME_SIZE_SAMPLES, 0.0f);
    auto nextWake = std::chrono::steady_clock::now();

    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_stateMutex);
            m_stateCondition.wait(lock, [this]() {
                return m_stopRequested || !m_paused;
            });
            if (m_stopRequested) {
                break;
            }
        }

        int samplesToWrite = 0;
        if (m_jitterBuffer != nullptr) {
            samplesToWrite = static_cast<int>(std::min(
                static_cast<unsigned>(frame.size()),
                m_jitterBuffer->samplesReadyForPlayback()));
            if (samplesToWrite > 0) {
                samplesToWrite = m_jitterBuffer->readSamples(frame.data(), samplesToWrite);
            }
        }

        if (samplesToWrite > 0) {
            writeSamplesBlocking(frame.data(), samplesToWrite);
        }

        nextWake += std::chrono::milliseconds(AudioEngine::FRAME_SIZE_MS);
        const auto now = std::chrono::steady_clock::now();
        if (nextWake > now) {
            std::this_thread::sleep_until(nextWake);
        } else {
            nextWake = now;
        }
    }
#endif
}

int AndroidAudioTrackOutput::writeSamplesBlocking(const float* samples, int count)
{
#if defined(Q_OS_ANDROID)
    if (samples == nullptr || count <= 0) {
        return 0;
    }

    if (!ensureSampleArrayCapacity(count, m_useFloatPlayback)) {
        return 0;
    }

    QJniEnvironment env;
    if (!env.isValid()) {
        return 0;
    }

    jclass klass = audioTrackPlayerClass(env);
    if (klass == nullptr) {
        return 0;
    }

    jint written;
    if (m_useFloatPlayback) {
        auto sampleArray = static_cast<jfloatArray>(m_sampleArrayGlobal);
        env->SetFloatArrayRegion(sampleArray, 0, count, reinterpret_cast<const jfloat*>(samples));
        if (env.checkAndClearExceptions()) {
            qWarning() << "AndroidAudioTrackOutput: SetFloatArrayRegion() failed";
            return 0;
        }

        jmethodID methodId = writePcmFloatMethod(env);
        if (methodId == nullptr) {
            return 0;
        }

        written = env->CallStaticIntMethod(klass, methodId, sampleArray, count);
    } else {
        if (m_pcm16Buffer.size() < static_cast<size_t>(count)) {
            m_pcm16Buffer.resize(static_cast<size_t>(count));
        }

        constexpr float kScale = 32767.0f;
        for (int i = 0; i < count; ++i) {
            const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
            m_pcm16Buffer[static_cast<size_t>(i)] = static_cast<short>(clamped * kScale);
        }

        auto sampleArray = static_cast<jshortArray>(m_sampleArrayGlobal);
        env->SetShortArrayRegion(sampleArray, 0, count, reinterpret_cast<const jshort*>(m_pcm16Buffer.data()));
        if (env.checkAndClearExceptions()) {
            qWarning() << "AndroidAudioTrackOutput: SetShortArrayRegion() failed";
            return 0;
        }

        jmethodID methodId = writePcm16Method(env);
        if (methodId == nullptr) {
            return 0;
        }

        written = env->CallStaticIntMethod(klass, methodId, sampleArray, count);
    }

    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: write() threw an exception";
        return 0;
    }

    if (written < 0) {
        qWarning() << "AndroidAudioTrackOutput: write() returned error" << written;
        return 0;
    }

    return static_cast<int>(written);
#else
    Q_UNUSED(samples)
    Q_UNUSED(count)
    return 0;
#endif
}

bool AndroidAudioTrackOutput::ensureSampleArrayCapacity(int sampleCount, bool useFloat)
{
#if defined(Q_OS_ANDROID)
    if (sampleCount <= 0) {
        return false;
    }

    if (m_sampleArrayGlobal != nullptr
            && m_sampleArrayIsFloat == useFloat
            && m_sampleArraySize >= sampleCount) {
        return true;
    }

    releaseSampleArray();

    QJniEnvironment env;
    if (!env.isValid()) {
        return false;
    }

    jobject localArray = useFloat
            ? static_cast<jobject>(env->NewFloatArray(sampleCount))
            : static_cast<jobject>(env->NewShortArray(sampleCount));
    if (localArray == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to allocate"
                   << (useFloat ? "float" : "short") << "sample array";
        return false;
    }

    auto globalArray = env->NewGlobalRef(localArray);
    env->DeleteLocalRef(localArray);
    if (globalArray == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioTrackOutput: Failed to promote sample array to global ref";
        return false;
    }

    m_sampleArrayGlobal = globalArray;
    m_sampleArraySize = sampleCount;
    m_sampleArrayIsFloat = useFloat;
    return true;
#else
    Q_UNUSED(sampleCount)
    Q_UNUSED(useFloat)
    return false;
#endif
}

void AndroidAudioTrackOutput::releaseSampleArray()
{
#if defined(Q_OS_ANDROID)
    if (m_sampleArrayGlobal == nullptr) {
        return;
    }

    QJniEnvironment env;
    if (env.isValid()) {
        env->DeleteGlobalRef(static_cast<jobject>(m_sampleArrayGlobal));
        env.checkAndClearExceptions();
    }

    m_sampleArrayGlobal = nullptr;
    m_sampleArraySize = 0;
    m_sampleArrayIsFloat = false;
#endif
}

QString AndroidAudioTrackOutput::currentRoute() const
{
#if defined(Q_OS_ANDROID)
    const QJniObject routeObject = QJniObject::callStaticObjectMethod(
        kRouteManagerClass,
        "getCurrentRoute",
        "()Ljava/lang/String;");

    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioTrackOutput: getCurrentRoute() threw an exception";
    }

    const QString route = routeObject.toString().trimmed();
    return route.isEmpty() ? QString::fromLatin1(kSpeakerRoute) : route;
#else
    return QString();
#endif
}
