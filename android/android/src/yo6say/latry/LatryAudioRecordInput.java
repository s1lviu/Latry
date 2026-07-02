package yo6say.latry;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Build;
import android.util.Log;

public final class LatryAudioRecordInput {
    private static final String TAG = "LatryAudioRecord";
    private static final int CHANNEL_MASK = AudioFormat.CHANNEL_IN_MONO;
    private static final int BUFFER_MULTIPLIER = 2;

    private static final Object lock = new Object();
    private static AudioRecord audioRecord;
    private static Context appContext;
    private static String currentRouteId = LatryAudioRoutePolicy.ROUTE_SPEAKER;
    private static int currentAudioSource = MediaRecorder.AudioSource.DEFAULT;
    private static int currentSampleRate = 16000;
    private static int currentEncoding = AudioFormat.ENCODING_PCM_16BIT;
    private static boolean mediaTekAudioModeApplied = false;
    private static int savedAudioMode = AudioManager.MODE_NORMAL;

    private LatryAudioRecordInput() {
    }

    public static boolean prepareCapture(Context context, String routeId) {
        synchronized (lock) {
            ensureInitialized(context);
            currentRouteId = normalizeRouteId(routeId);

            if (!hasRecordAudioPermission()) {
                Log.d(TAG, "Skipping AudioRecord pre-warm until RECORD_AUDIO permission is granted");
                return false;
            }

            if (audioRecord != null && audioRecord.getState() == AudioRecord.STATE_INITIALIZED) {
                boolean preferredDeviceApplied = applyPreferredDeviceLocked(currentRouteId);
                return preferredDeviceApplied || !LatryAudioRoutePolicy.ROUTE_BLUETOOTH.equals(currentRouteId);
            }

            releaseCaptureLocked(false);

            AudioRecord record = createAudioRecordLocked();
            if (record == null) {
                Log.w(TAG, "Failed to create AudioRecord during pre-warm");
                return false;
            }

            audioRecord = record;
            boolean preferredDeviceApplied = applyPreferredDeviceLocked(currentRouteId);
            if (!preferredDeviceApplied && LatryAudioRoutePolicy.ROUTE_BLUETOOTH.equals(currentRouteId)) {
                Log.w(TAG, "Bluetooth capture requested but no Bluetooth microphone could be selected");
                releaseCaptureLocked(false);
                return false;
            }
            Log.i(TAG, "AudioRecord pre-warmed @ " + currentSampleRate + " Hz format="
                    + encodingName(currentEncoding) + " route=" + currentRouteId);
            return true;
        }
    }

    public static boolean startCapture(Context context, String routeId) {
        synchronized (lock) {
            ensureInitialized(context);
            currentRouteId = normalizeRouteId(routeId);
            if (!LatryAudioRouteManager.prepareCaptureRoute(appContext, currentRouteId)) {
                Log.w(TAG, "Capture route is not ready for route " + currentRouteId);
                LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
                return false;
            }

            if (LatryAudioRoutePolicy.ROUTE_BLUETOOTH.equals(currentRouteId)
                    && !hasBluetoothPreferredDeviceLocked()) {
                releaseCaptureLocked(false);
            }

            if (!prepareCapture(context, routeId)) {
                LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
                return false;
            }

            if (audioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                return true;
            }

            applyMediaTekPocAudioModeLocked();

            try {
                audioRecord.startRecording();
            } catch (Exception e) {
                Log.e(TAG, "AudioRecord.startRecording() failed", e);
                releaseCaptureLocked(false);
                LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
                return false;
            }

            if (audioRecord.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
                Log.w(TAG, "AudioRecord did not enter RECORDSTATE_RECORDING");
                releaseCaptureLocked(false);
                LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
                return false;
            }

            Log.i(TAG, "AudioRecord started @ " + currentSampleRate + " Hz format="
                    + encodingName(currentEncoding) + " on route " + currentRouteId);
            return true;
        }
    }

    public static void stopCapture() {
        synchronized (lock) {
            stopCaptureLocked();
        }
    }

    public static void releaseCapture() {
        synchronized (lock) {
            releaseCaptureLocked();
        }
    }

    public static int readPcm16(short[] samples, int sampleCount) {
        AudioRecord localRecord;
        int localEncoding;
        synchronized (lock) {
            localRecord = audioRecord;
            localEncoding = currentEncoding;
        }

        if (localRecord == null
                || localRecord.getState() != AudioRecord.STATE_INITIALIZED
                || localEncoding != AudioFormat.ENCODING_PCM_16BIT
                || samples == null
                || sampleCount <= 0) {
            return 0;
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                return localRecord.read(samples, 0, sampleCount, AudioRecord.READ_BLOCKING);
            }
            return localRecord.read(samples, 0, sampleCount);
        } catch (Exception e) {
            Log.e(TAG, "AudioRecord.read() failed", e);
            return 0;
        }
    }

    public static int readPcmFloat(float[] samples, int sampleCount) {
        AudioRecord localRecord;
        int localEncoding;
        synchronized (lock) {
            localRecord = audioRecord;
            localEncoding = currentEncoding;
        }

        if (localRecord == null
                || localRecord.getState() != AudioRecord.STATE_INITIALIZED
                || localEncoding != AudioFormat.ENCODING_PCM_FLOAT
                || samples == null
                || sampleCount <= 0
                || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return 0;
        }

        try {
            return localRecord.read(samples, 0, sampleCount, AudioRecord.READ_BLOCKING);
        } catch (Exception e) {
            Log.e(TAG, "AudioRecord.read(float) failed", e);
            return 0;
        }
    }

    public static int getSampleRate() {
        synchronized (lock) {
            return currentSampleRate;
        }
    }

    public static int getEncoding() {
        synchronized (lock) {
            return currentEncoding;
        }
    }

    static boolean hasActiveRecordForTesting() {
        synchronized (lock) {
            return audioRecord != null && audioRecord.getState() == AudioRecord.STATE_INITIALIZED;
        }
    }

    static boolean isRecordingForTesting() {
        synchronized (lock) {
            return audioRecord != null
                    && audioRecord.getState() == AudioRecord.STATE_INITIALIZED
                    && audioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING;
        }
    }

    static String getCurrentRouteForTesting() {
        synchronized (lock) {
            return currentRouteId;
        }
    }

    static int getPreferredDeviceTypeForTesting() {
        synchronized (lock) {
            if (audioRecord == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                return 0;
            }

            AudioDeviceInfo deviceInfo = audioRecord.getPreferredDevice();
            return deviceInfo != null ? deviceInfo.getType() : 0;
        }
    }

    static int getCurrentAudioSourceForTesting() {
        synchronized (lock) {
            return currentAudioSource;
        }
    }

    private static void ensureInitialized(Context context) {
        if (appContext == null && context != null) {
            appContext = context.getApplicationContext();
        }
    }

    private static boolean hasRecordAudioPermission() {
        if (appContext == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        return appContext.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    }

    private static AudioRecord createAudioRecordLocked() {
        boolean mediaTekPocProfile = usesMediaTekPocProfileLocked();
        int[] audioSources = LatryAudioCaptureProfile.audioSources(mediaTekPocProfile);
        int[] sampleRates = LatryAudioCaptureProfile.sampleRates(mediaTekPocProfile);

        Log.i(TAG, "AudioRecord capture profile=" + LatryAudioCaptureProfile.name(mediaTekPocProfile)
                + " hardware=" + safeBuildValue(Build.HARDWARE)
                + " board=" + safeBuildValue(Build.BOARD)
                + " manufacturer=" + safeBuildValue(Build.MANUFACTURER));

        for (int audioSource : audioSources) {
            for (int sampleRate : sampleRates) {
                for (int encoding : encodingsToTry()) {
                    int minBufferSize = AudioRecord.getMinBufferSize(sampleRate, CHANNEL_MASK, encoding);
                    if (minBufferSize <= 0) {
                        continue;
                    }

                    int bufferSize = minBufferSize * BUFFER_MULTIPLIER;
                    AudioRecord record = buildAudioRecord(audioSource, sampleRate, encoding, bufferSize);
                    if (record == null) {
                        continue;
                    }

                    if (record.getState() == AudioRecord.STATE_INITIALIZED) {
                        currentAudioSource = audioSource;
                        currentSampleRate = sampleRate;
                        currentEncoding = encoding;
                        Log.i(TAG, "Using AudioRecord source=" + sourceName(audioSource)
                                + " rate=" + sampleRate
                                + " format=" + encodingName(encoding)
                                + " via " + (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? "Builder" : "legacy"));
                        return record;
                    }

                    try {
                        record.release();
                    } catch (Exception e) {
                        Log.w(TAG, "AudioRecord.release() failed during fallback", e);
                    }
                }
            }
        }

        return null;
    }

    private static boolean usesMediaTekPocProfileLocked() {
        String socModel = "";
        String socManufacturer = "";
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            socModel = Build.SOC_MODEL;
            socManufacturer = Build.SOC_MANUFACTURER;
        }

        return LatryAudioCaptureProfile.usesMediaTekPocProfile(
                Build.HARDWARE,
                Build.BOARD,
                Build.MANUFACTURER,
                socModel,
                socManufacturer);
    }

    private static int[] encodingsToTry() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return new int[] {AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_16BIT};
        }
        return new int[] {AudioFormat.ENCODING_PCM_16BIT};
    }

    private static AudioRecord buildAudioRecord(int audioSource, int sampleRate, int encoding, int bufferSize) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                AudioFormat format = new AudioFormat.Builder()
                        .setEncoding(encoding)
                        .setChannelMask(CHANNEL_MASK)
                        .setSampleRate(sampleRate)
                        .build();

                AudioRecord.Builder builder = new AudioRecord.Builder()
                        .setAudioSource(audioSource)
                        .setAudioFormat(format)
                        .setBufferSizeInBytes(bufferSize);

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    builder.setPrivacySensitive(true);
                }

                return builder.build();
            }

            if (encoding != AudioFormat.ENCODING_PCM_16BIT) {
                return null;
            }

            return new AudioRecord(audioSource, sampleRate, CHANNEL_MASK, encoding, bufferSize);
        } catch (Exception e) {
            Log.w(TAG, "Failed to build AudioRecord source=" + sourceName(audioSource)
                    + " rate=" + sampleRate + " format=" + encodingName(encoding), e);
            return null;
        }
    }

    private static String normalizeRouteId(String routeId) {
        return LatryAudioRoutePolicy.normalizeRouteId(routeId);
    }

    private static boolean applyPreferredDeviceLocked(String routeId) {
        if (audioRecord == null || appContext == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return false;
        }

        AudioDeviceInfo deviceInfo = LatryAudioRouteManager.findCaptureDevice(appContext, routeId);
        String selectedLabel = routeId;
        if (deviceInfo == null && LatryAudioRoutePolicy.ROUTE_WIRED_HEADSET.equals(routeId)) {
            deviceInfo = LatryAudioRouteManager.findBuiltInMic(appContext);
            selectedLabel = "built_in_mic_fallback";
        }

        if (deviceInfo == null) {
            Log.w(TAG, "No capture device found for route " + routeId + ", clearing preferred device");
            audioRecord.setPreferredDevice(null);
            return false;
        }

        boolean success = audioRecord.setPreferredDevice(deviceInfo);
        Log.i(TAG, "setPreferredDevice(" + selectedLabel + ", type=" + deviceInfo.getType() + ") -> " + success);
        return success;
    }

    private static boolean hasBluetoothPreferredDeviceLocked() {
        if (audioRecord == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return false;
        }

        AudioDeviceInfo deviceInfo = audioRecord.getPreferredDevice();
        if (deviceInfo == null) {
            return false;
        }

        int deviceType = deviceInfo.getType();
        return deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || deviceType == 26;
    }

    private static void applyMediaTekPocAudioModeLocked() {
        if (!usesMediaTekPocProfileLocked() || appContext == null) {
            return;
        }

        Object systemService = appContext.getSystemService(Context.AUDIO_SERVICE);
        if (!(systemService instanceof AudioManager)) {
            return;
        }

        AudioManager audioManager = (AudioManager) systemService;
        try {
            if (!mediaTekAudioModeApplied) {
                savedAudioMode = audioManager.getMode();
                mediaTekAudioModeApplied = true;
            }
            audioManager.setMode(AudioManager.MODE_NORMAL);
            Log.i(TAG, "Applied MediaTek PoC capture audio mode: MODE_NORMAL"
                    + " savedMode=" + savedAudioMode
                    + " route=" + currentRouteId);
        } catch (Exception e) {
            Log.w(TAG, "Failed to apply MediaTek PoC capture audio mode", e);
        }
    }

    private static void restoreMediaTekPocAudioModeLocked() {
        if (!mediaTekAudioModeApplied || appContext == null) {
            mediaTekAudioModeApplied = false;
            savedAudioMode = AudioManager.MODE_NORMAL;
            return;
        }

        Object systemService = appContext.getSystemService(Context.AUDIO_SERVICE);
        if (systemService instanceof AudioManager) {
            try {
                ((AudioManager) systemService).setMode(savedAudioMode);
                Log.i(TAG, "Restored audio mode after MediaTek PoC capture: " + savedAudioMode);
            } catch (Exception e) {
                Log.w(TAG, "Failed to restore audio mode after MediaTek PoC capture", e);
            }
        }

        mediaTekAudioModeApplied = false;
        savedAudioMode = AudioManager.MODE_NORMAL;
    }

    private static void stopCaptureLocked() {
        stopCaptureLocked(true);
    }

    private static void stopCaptureLocked(boolean finishRoute) {
        if (audioRecord == null) {
            restoreMediaTekPocAudioModeLocked();
            if (finishRoute && appContext != null) {
                LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
            }
            return;
        }

        try {
            if (audioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                audioRecord.stop();
                Log.i(TAG, "AudioRecord stopped (kept warm for next PTT)");
            }
        } catch (Exception e) {
            Log.w(TAG, "AudioRecord.stop() failed during stop", e);
        }

        restoreMediaTekPocAudioModeLocked();

        if (finishRoute && appContext != null) {
            LatryAudioRouteManager.finishCaptureRoute(appContext, currentRouteId);
        }
    }

    private static void releaseCaptureLocked() {
        releaseCaptureLocked(true);
    }

    private static void releaseCaptureLocked(boolean finishRoute) {
        if (audioRecord == null) {
            return;
        }

        stopCaptureLocked(finishRoute);

        try {
            audioRecord.release();
        } catch (Exception e) {
            Log.w(TAG, "AudioRecord.release() failed", e);
        }

        audioRecord = null;
        currentAudioSource = MediaRecorder.AudioSource.DEFAULT;
        currentSampleRate = 16000;
        currentEncoding = AudioFormat.ENCODING_PCM_16BIT;
        restoreMediaTekPocAudioModeLocked();
    }

    private static String sourceName(int audioSource) {
        if (audioSource == MediaRecorder.AudioSource.MIC) {
            return "MIC";
        }
        if (audioSource == MediaRecorder.AudioSource.DEFAULT) {
            return "DEFAULT";
        }
        if (audioSource == MediaRecorder.AudioSource.VOICE_RECOGNITION) {
            return "VOICE_RECOGNITION";
        }
        return String.valueOf(audioSource);
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

    private static String safeBuildValue(String value) {
        return value == null ? "" : value;
    }
}
