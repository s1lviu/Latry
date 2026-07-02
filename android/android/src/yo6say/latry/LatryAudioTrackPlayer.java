package yo6say.latry;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Build;
import android.util.Log;

public final class LatryAudioTrackPlayer {
    private static final String TAG = "LatryAudioTrack";
    private static final int SAMPLE_RATE = 16000;
    private static final int CHANNEL_MASK = AudioFormat.CHANNEL_OUT_MONO;
    private static final int BUFFER_MULTIPLIER = 4;

    private static final Object lock = new Object();
    private static AudioTrack audioTrack;
    private static Context appContext;
    private static String currentRouteId = LatryAudioRoutePolicy.ROUTE_SPEAKER;
    private static int currentContentType = AudioAttributes.CONTENT_TYPE_UNKNOWN;
    private static int currentEncoding = AudioFormat.ENCODING_PCM_16BIT;

    private LatryAudioTrackPlayer() {
    }

    public static boolean startPlayback(Context context, String routeId) {
        synchronized (lock) {
            ensureInitialized(context);
            stopPlaybackLocked();

            int[] encodings;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                encodings = new int[] {AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_16BIT};
            } else {
                encodings = new int[] {AudioFormat.ENCODING_PCM_16BIT};
            }

            for (int encoding : encodings) {
                if (tryBuildTrack(encoding)) {
                    currentRouteId = normalizeRouteId(routeId);
                    applyPreferredDeviceLocked(currentRouteId);

                    try {
                        audioTrack.play();
                    } catch (Exception e) {
                        Log.e(TAG, "AudioTrack.play() failed", e);
                        stopPlaybackLocked();
                        return false;
                    }

                    Log.i(TAG, "AudioTrack started @ " + SAMPLE_RATE + " Hz format="
                            + encodingName(currentEncoding) + " on route " + currentRouteId);
                    return true;
                }
            }

            Log.w(TAG, "Failed to create AudioTrack with any encoding");
            return false;
        }
    }

    private static boolean tryBuildTrack(int encoding) {
        int minBufferSize = AudioTrack.getMinBufferSize(SAMPLE_RATE, CHANNEL_MASK, encoding);
        if (minBufferSize <= 0) {
            return false;
        }

        int bufferSize = minBufferSize * BUFFER_MULTIPLIER;
        AudioAttributes audioAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build();
        AudioFormat audioFormat = new AudioFormat.Builder()
                .setSampleRate(SAMPLE_RATE)
                .setEncoding(encoding)
                .setChannelMask(CHANNEL_MASK)
                .build();

        AudioTrack.Builder builder = new AudioTrack.Builder()
                .setAudioAttributes(audioAttributes)
                .setAudioFormat(audioFormat)
                .setBufferSizeInBytes(bufferSize)
                .setTransferMode(AudioTrack.MODE_STREAM);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);
        }

        try {
            audioTrack = builder.build();
        } catch (Exception e) {
            Log.w(TAG, "Failed to build AudioTrack encoding=" + encodingName(encoding), e);
            audioTrack = null;
            return false;
        }

        if (audioTrack == null || audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
            stopPlaybackLocked();
            return false;
        }

        currentEncoding = encoding;
        currentContentType = AudioAttributes.CONTENT_TYPE_SPEECH;
        return true;
    }

    public static void stopPlayback() {
        synchronized (lock) {
            stopPlaybackLocked();
        }
    }

    public static void pausePlayback() {
        synchronized (lock) {
            if (audioTrack == null || audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
                return;
            }

            try {
                if (audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                    audioTrack.pause();
                }
            } catch (Exception e) {
                Log.w(TAG, "AudioTrack.pause() failed", e);
            }
        }
    }

    public static void resumePlayback() {
        synchronized (lock) {
            if (audioTrack == null || audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
                return;
            }

            try {
                if (audioTrack.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) {
                    audioTrack.play();
                }
            } catch (Exception e) {
                Log.w(TAG, "AudioTrack.play() failed on resume", e);
            }
        }
    }

    public static int writePcm16(short[] samples, int sampleCount) {
        synchronized (lock) {
            if (audioTrack == null
                    || audioTrack.getState() != AudioTrack.STATE_INITIALIZED
                    || samples == null
                    || sampleCount <= 0) {
                return 0;
            }

            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    return audioTrack.write(samples, 0, sampleCount, AudioTrack.WRITE_BLOCKING);
                }
                return audioTrack.write(samples, 0, sampleCount);
            } catch (Exception e) {
                Log.e(TAG, "AudioTrack.write() failed", e);
                return 0;
            }
        }
    }

    public static int writePcmFloat(float[] samples, int sampleCount) {
        synchronized (lock) {
            if (audioTrack == null
                    || audioTrack.getState() != AudioTrack.STATE_INITIALIZED
                    || currentEncoding != AudioFormat.ENCODING_PCM_FLOAT
                    || samples == null
                    || sampleCount <= 0
                    || Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                return 0;
            }

            try {
                return audioTrack.write(samples, 0, sampleCount, AudioTrack.WRITE_BLOCKING);
            } catch (Exception e) {
                Log.e(TAG, "AudioTrack.write(float) failed", e);
                return 0;
            }
        }
    }

    public static int getEncoding() {
        synchronized (lock) {
            return currentEncoding;
        }
    }

    public static boolean setPlaybackRoute(Context context, String routeId) {
        synchronized (lock) {
            ensureInitialized(context);
            currentRouteId = normalizeRouteId(routeId);
            return applyPreferredDeviceLocked(currentRouteId);
        }
    }

    static boolean hasActiveTrackForTesting() {
        synchronized (lock) {
            return audioTrack != null && audioTrack.getState() == AudioTrack.STATE_INITIALIZED;
        }
    }

    static String getCurrentRouteForTesting() {
        synchronized (lock) {
            return currentRouteId;
        }
    }

    static int getPreferredDeviceTypeForTesting() {
        synchronized (lock) {
            if (audioTrack == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                return 0;
            }

            AudioDeviceInfo deviceInfo = audioTrack.getPreferredDevice();
            return deviceInfo != null ? deviceInfo.getType() : 0;
        }
    }

    static int getContentTypeForTesting() {
        synchronized (lock) {
            return currentContentType;
        }
    }

    private static void ensureInitialized(Context context) {
        if (appContext == null && context != null) {
            appContext = context.getApplicationContext();
        }
    }

    private static String normalizeRouteId(String routeId) {
        return LatryAudioRoutePolicy.normalizeRouteId(routeId);
    }

    private static boolean applyPreferredDeviceLocked(String routeId) {
        if (audioTrack == null || appContext == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return false;
        }

        AudioDeviceInfo deviceInfo = LatryAudioRouteManager.findPlaybackDevice(appContext, routeId);
        if (deviceInfo == null) {
            Log.w(TAG, "No playback device found for route " + routeId + ", clearing preferred device");
            return audioTrack.setPreferredDevice(null);
        }

        boolean success = audioTrack.setPreferredDevice(deviceInfo);
        Log.i(TAG, "setPreferredDevice(" + routeId + ", type=" + deviceInfo.getType() + ") -> " + success);
        return success;
    }

    private static void stopPlaybackLocked() {
        if (audioTrack == null) {
            return;
        }

        try {
            if (audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                audioTrack.pause();
            }
        } catch (Exception e) {
            Log.w(TAG, "AudioTrack.pause() failed during stop", e);
        }

        try {
            audioTrack.flush();
        } catch (Exception e) {
            Log.w(TAG, "AudioTrack.flush() failed during stop", e);
        }

        try {
            if (audioTrack.getState() == AudioTrack.STATE_INITIALIZED) {
                audioTrack.stop();
            }
        } catch (Exception e) {
            Log.w(TAG, "AudioTrack.stop() failed during stop", e);
        }

        try {
            audioTrack.release();
        } catch (Exception e) {
            Log.w(TAG, "AudioTrack.release() failed", e);
        }

        audioTrack = null;
        currentContentType = AudioAttributes.CONTENT_TYPE_UNKNOWN;
        currentEncoding = AudioFormat.ENCODING_PCM_16BIT;
    }

    private static String encodingName(int encoding) {
        if (encoding == AudioFormat.ENCODING_PCM_FLOAT) {
            return "FLOAT";
        }
        if (encoding == AudioFormat.ENCODING_PCM_16BIT) {
            return "INT16";
        }
        return String.valueOf(encoding);
    }
}
