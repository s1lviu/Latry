#include "AndroidAudioRecordInput.h"

#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include <QDebug>
#include <QString>

#if defined(Q_OS_ANDROID)
#  include <QtCore/QJniEnvironment>
#  include <QtCore/QJniObject>

namespace {
constexpr char kQtNativeClass[] = "org/qtproject/qt/android/QtNative";
constexpr char kRouteManagerClass[] = "yo6say/latry/LatryAudioRouteManager";
constexpr char kAudioRecordInputClassName[] = "yo6say.latry.LatryAudioRecordInput";
constexpr char kSpeakerRoute[] = "speaker";
constexpr jint kEncodingPcm16Bit = 2;
constexpr jint kEncodingPcmFloat = 4;

QJniObject androidContext()
{
    return QJniObject::callStaticObjectMethod(
        kQtNativeClass,
        "getContext",
        "()Landroid/content/Context;");
}

jclass audioRecordInputClass(QJniEnvironment& env)
{
    static jclass globalClass = nullptr;
    if (globalClass != nullptr) {
        return globalClass;
    }

    if (!env.isValid()) {
        qWarning() << "AndroidAudioRecordInput: JNI environment is not valid for class resolution";
        return nullptr;
    }

    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioRecordInput: Failed to get Android context for class resolution";
        return nullptr;
    }

    const QJniObject classLoader = context.callObjectMethod(
        "getClassLoader",
        "()Ljava/lang/ClassLoader;");
    if (!classLoader.isValid()) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to get application ClassLoader";
        return nullptr;
    }

    const QJniObject className = QJniObject::fromString(QString::fromLatin1(kAudioRecordInputClassName));
    const QJniObject classObject = classLoader.callObjectMethod(
        "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        className.object());
    if (!classObject.isValid()) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to load" << kAudioRecordInputClassName
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
        qWarning() << "AndroidAudioRecordInput: Failed to create global class reference";
    }
    return globalClass;
}

jmethodID prepareCaptureMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(
        klass,
        "prepareCapture",
        "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve prepareCapture()";
    }
    return methodId;
}

jmethodID startCaptureMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(
        klass,
        "startCapture",
        "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve startCapture()";
    }
    return methodId;
}

jmethodID stopCaptureMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "stopCapture", "()V");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve stopCapture()";
    }
    return methodId;
}

jmethodID releaseCaptureMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "releaseCapture", "()V");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve releaseCapture()";
    }
    return methodId;
}

jmethodID readPcm16Method(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "readPcm16", "([SI)I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve readPcm16()";
    }
    return methodId;
}

jmethodID readPcmFloatMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "readPcmFloat", "([FI)I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve readPcmFloat()";
    }
    return methodId;
}

jmethodID sampleRateMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "getSampleRate", "()I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve getSampleRate()";
    }
    return methodId;
}

jmethodID encodingMethod(QJniEnvironment& env)
{
    static jmethodID methodId = nullptr;
    if (methodId != nullptr) {
        return methodId;
    }

    jclass klass = audioRecordInputClass(env);
    if (klass == nullptr) {
        return nullptr;
    }

    methodId = env->GetStaticMethodID(klass, "getEncoding", "()I");
    if (methodId == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to resolve getEncoding()";
    }
    return methodId;
}

QString currentRoute()
{
    const QJniObject routeObject = QJniObject::callStaticObjectMethod(
        kRouteManagerClass,
        "getCurrentRoute",
        "()Ljava/lang/String;");

    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: getCurrentRoute() threw an exception";
    }

    const QString route = routeObject.toString().trimmed();
    return route.isEmpty() ? QString::fromLatin1(kSpeakerRoute) : route;
}

bool updateCaptureFormat(QJniEnvironment& env,
                         jclass klass,
                         jmethodID rateMethodId,
                         jmethodID encodingMethodId,
                         int& sampleRate,
                         bool& useFloatCapture)
{
    if (klass == nullptr || rateMethodId == nullptr || encodingMethodId == nullptr) {
        return false;
    }

    const jint detectedSampleRate = env->CallStaticIntMethod(klass, rateMethodId);
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: getSampleRate() threw an exception";
        return false;
    }

    const jint detectedEncoding = env->CallStaticIntMethod(klass, encodingMethodId);
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: getEncoding() threw an exception";
        return false;
    }

    if (detectedSampleRate <= 0) {
        qWarning() << "AndroidAudioRecordInput: Invalid sample rate" << detectedSampleRate;
        return false;
    }

    sampleRate = detectedSampleRate;
    useFloatCapture = (detectedEncoding == kEncodingPcmFloat);
    if (detectedEncoding != kEncodingPcmFloat && detectedEncoding != kEncodingPcm16Bit) {
        qWarning() << "AndroidAudioRecordInput: Unsupported capture encoding" << detectedEncoding
                   << "- falling back to PCM16 handling";
    }
    return true;
}
}
#endif

AndroidAudioRecordInput::AndroidAudioRecordInput(Int16SampleHandler int16SampleHandler,
                                                 FloatSampleHandler floatSampleHandler)
    : m_int16SampleHandler(std::move(int16SampleHandler))
    , m_floatSampleHandler(std::move(floatSampleHandler))
{
}

AndroidAudioRecordInput::~AndroidAudioRecordInput()
{
    release();
}

bool AndroidAudioRecordInput::prepare()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioRecordInput: Failed to get Android context";
        return false;
    }

    QJniEnvironment env;
    jclass klass = audioRecordInputClass(env);
    jmethodID prepareMethodId = prepareCaptureMethod(env);
    jmethodID rateMethodId = sampleRateMethod(env);
    jmethodID encodingMethodId = encodingMethod(env);
    if (klass == nullptr || prepareMethodId == nullptr || rateMethodId == nullptr
            || encodingMethodId == nullptr) {
        qWarning() << "AndroidAudioRecordInput: Failed to resolve prepareCapture JNI bindings";
        return false;
    }

    const QJniObject routeString = QJniObject::fromString(currentRoute());
    const jboolean prepared = env->CallStaticBooleanMethod(
        klass,
        prepareMethodId,
        context.object(),
        routeString.object());
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: prepareCapture() threw an exception";
        return false;
    }

    if (!prepared) {
        return false;
    }

    int detectedSampleRate = 0;
    bool useFloatCapture = false;
    if (!updateCaptureFormat(env, klass, rateMethodId, encodingMethodId,
                             detectedSampleRate, useFloatCapture)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_sampleRate = detectedSampleRate;
    m_useFloatCapture = useFloatCapture;
    return true;
#else
    return false;
#endif
}

bool AndroidAudioRecordInput::start()
{
#if defined(Q_OS_ANDROID)
    stop();

    if (!prepare()) {
        return false;
    }

    const bool expectFloatCapture = usesFloatCapture();

    const QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "AndroidAudioRecordInput: Failed to get Android context";
        return false;
    }

    QJniEnvironment env;
    jclass klass = audioRecordInputClass(env);
    jmethodID startMethodId = startCaptureMethod(env);
    jmethodID stopMethodId = stopCaptureMethod(env);
    jmethodID rateMethodId = sampleRateMethod(env);
    jmethodID encodingMethodId = encodingMethod(env);
    jmethodID readInt16MethodId = readPcm16Method(env);
    jmethodID readFloatMethodId = readPcmFloatMethod(env);
    if (klass == nullptr || startMethodId == nullptr || stopMethodId == nullptr
            || rateMethodId == nullptr || encodingMethodId == nullptr
            || readInt16MethodId == nullptr
            || (expectFloatCapture && readFloatMethodId == nullptr)) {
        qWarning() << "AndroidAudioRecordInput: Failed to resolve LatryAudioRecordInput JNI bindings";
        return false;
    }

    const QJniObject routeString = QJniObject::fromString(currentRoute());
    const jboolean started = env->CallStaticBooleanMethod(
        klass,
        startMethodId,
        context.object(),
        routeString.object());
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: startCapture() threw an exception";
        return false;
    }

    if (!started) {
        qWarning() << "AndroidAudioRecordInput: startCapture() failed";
        return false;
    }

    int detectedSampleRate = 0;
    bool useFloatCapture = false;
    if (!updateCaptureFormat(env, klass, rateMethodId, encodingMethodId,
                             detectedSampleRate, useFloatCapture)) {
        env->CallStaticVoidMethod(klass, stopMethodId);
        env.checkAndClearExceptions();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_sampleRate = detectedSampleRate;
        m_useFloatCapture = useFloatCapture;
        m_capturing = true;
        m_stopRequested = false;
    }

    m_captureThread = std::thread(&AndroidAudioRecordInput::captureLoop, this);
    return true;
#else
    return false;
#endif
}

void AndroidAudioRecordInput::stop()
{
#if defined(Q_OS_ANDROID)
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_stopRequested = true;
    }

    QJniEnvironment env;
    jclass klass = audioRecordInputClass(env);
    jmethodID stopMethodId = stopCaptureMethod(env);
    if (klass != nullptr && stopMethodId != nullptr) {
        env->CallStaticVoidMethod(klass, stopMethodId);
    }
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: stopCapture() threw an exception";
    }

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    releaseSampleArray();

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_capturing = false;
    m_stopRequested = false;
#endif
}

void AndroidAudioRecordInput::release()
{
#if defined(Q_OS_ANDROID)
    stop();

    QJniEnvironment env;
    jclass klass = audioRecordInputClass(env);
    jmethodID releaseMethodId = releaseCaptureMethod(env);
    if (klass != nullptr && releaseMethodId != nullptr) {
        env->CallStaticVoidMethod(klass, releaseMethodId);
    }
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidAudioRecordInput: releaseCapture() threw an exception";
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_sampleRate = AudioEngine::SAMPLE_RATE;
    m_useFloatCapture = false;
#endif
}

bool AndroidAudioRecordInput::isCapturing() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_capturing;
}

int AndroidAudioRecordInput::sampleRate() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_sampleRate;
}

bool AndroidAudioRecordInput::usesFloatCapture() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_useFloatCapture;
}

void AndroidAudioRecordInput::captureLoop()
{
#if defined(Q_OS_ANDROID)
    QJniEnvironment env;
    if (!env.isValid()) {
        qWarning() << "AndroidAudioRecordInput: Failed to attach capture thread to JVM";
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_capturing = false;
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
                    qWarning() << "AndroidAudioRecordInput: setThreadPriority(-19) threw an exception";
                }
            }
            env->DeleteLocalRef(processClass);
        } else {
            env.checkAndClearExceptions();
        }
    }

    jclass klass = audioRecordInputClass(env);
    jmethodID readInt16MethodId = readPcm16Method(env);
    jmethodID readFloatMethodId = readPcmFloatMethod(env);
    if (klass == nullptr || readInt16MethodId == nullptr || readFloatMethodId == nullptr) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_capturing = false;
        return;
    }

    const int captureSampleRate = sampleRate();
    const bool useFloatSamples = usesFloatCapture();
    const int frameSizeSamples = std::max(1, captureSampleRate * AudioEngine::FRAME_SIZE_MS / 1000);

    while (true) {
        bool stopAfterRead = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            stopAfterRead = m_stopRequested;
        }

        if (!ensureSampleArrayCapacity(frameSizeSamples, useFloatSamples)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        const jint samplesRead = useFloatSamples
                ? env->CallStaticIntMethod(klass, readFloatMethodId,
                                           static_cast<jfloatArray>(m_sampleArrayGlobal),
                                           frameSizeSamples)
                : env->CallStaticIntMethod(klass, readInt16MethodId,
                                           static_cast<jshortArray>(m_sampleArrayGlobal),
                                           frameSizeSamples);
        if (env.checkAndClearExceptions()) {
            qWarning() << "AndroidAudioRecordInput:" << (useFloatSamples ? "readPcmFloat()" : "readPcm16()")
                       << "threw an exception";
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            stopAfterRead = stopAfterRead || m_stopRequested;
        }

        if (samplesRead > 0) {
            if (useFloatSamples) {
                if (m_floatBuffer.size() < static_cast<size_t>(samplesRead)) {
                    m_floatBuffer.resize(static_cast<size_t>(samplesRead));
                }

                env->GetFloatArrayRegion(static_cast<jfloatArray>(m_sampleArrayGlobal), 0, samplesRead,
                                         reinterpret_cast<jfloat*>(m_floatBuffer.data()));
                if (env.checkAndClearExceptions()) {
                    qWarning() << "AndroidAudioRecordInput: GetFloatArrayRegion() failed";
                    continue;
                }

                if (m_floatSampleHandler) {
                    m_floatSampleHandler(m_floatBuffer.data(), static_cast<int>(samplesRead), captureSampleRate);
                }
            } else {
                if (m_pcm16Buffer.size() < static_cast<size_t>(samplesRead)) {
                    m_pcm16Buffer.resize(static_cast<size_t>(samplesRead));
                }

                env->GetShortArrayRegion(static_cast<jshortArray>(m_sampleArrayGlobal), 0, samplesRead,
                                         reinterpret_cast<jshort*>(m_pcm16Buffer.data()));
                if (env.checkAndClearExceptions()) {
                    qWarning() << "AndroidAudioRecordInput: GetShortArrayRegion() failed";
                    continue;
                }

                if (m_int16SampleHandler) {
                    m_int16SampleHandler(m_pcm16Buffer.data(), static_cast<int>(samplesRead), captureSampleRate);
                }
            }

            if (stopAfterRead) {
                break;
            }
            continue;
        }

        if (samplesRead < 0) {
            qWarning() << "AndroidAudioRecordInput:" << (useFloatSamples ? "readPcmFloat()" : "readPcm16()")
                       << "returned error" << samplesRead;
        }

        if (stopAfterRead) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
}

bool AndroidAudioRecordInput::ensureSampleArrayCapacity(int sampleCount, bool useFloatSamples)
{
#if defined(Q_OS_ANDROID)
    if (sampleCount <= 0) {
        return false;
    }

    if (m_sampleArrayGlobal != nullptr
            && m_sampleArrayUsesFloat == useFloatSamples
            && m_sampleArraySize >= sampleCount) {
        return true;
    }

    releaseSampleArray();

    QJniEnvironment env;
    if (!env.isValid()) {
        return false;
    }

    jobject localArray = useFloatSamples
            ? static_cast<jobject>(env->NewFloatArray(sampleCount))
            : static_cast<jobject>(env->NewShortArray(sampleCount));
    if (localArray == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to allocate" << (useFloatSamples ? "float" : "short")
                   << "sample array";
        return false;
    }

    jobject globalArray = env->NewGlobalRef(localArray);
    env->DeleteLocalRef(localArray);
    if (globalArray == nullptr) {
        env.checkAndClearExceptions();
        qWarning() << "AndroidAudioRecordInput: Failed to promote sample array to global ref";
        return false;
    }

    m_sampleArrayGlobal = globalArray;
    m_sampleArraySize = sampleCount;
    m_sampleArrayUsesFloat = useFloatSamples;
    return true;
#else
    Q_UNUSED(sampleCount)
    Q_UNUSED(useFloatSamples)
    return false;
#endif
}

void AndroidAudioRecordInput::releaseSampleArray()
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
    m_sampleArrayUsesFloat = false;
#endif
}
