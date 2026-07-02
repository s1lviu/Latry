package yo6say.latry;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.speech.ModelDownloadListener;
import android.speech.RecognitionSupport;
import android.speech.RecognitionSupportCallback;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.util.Log;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

public final class LatryTranscriptionManager {
    private static final String TAG = "LatryTranscription";
    private static final Handler MAIN_HANDLER = new Handler(Looper.getMainLooper());
    private static final long SUPPORT_CHECK_TIMEOUT_MS = 3000L;
    private static final Object DOWNLOAD_STATE_LOCK = new Object();
    private static final int DOWNLOAD_STATE_IDLE = 0;
    private static final int DOWNLOAD_STATE_REQUESTED = 1;
    private static final int DOWNLOAD_STATE_IN_PROGRESS = 2;
    private static final int DOWNLOAD_STATE_SCHEDULED = 3;
    private static final int DOWNLOAD_STATE_READY = 4;
    private static final int DOWNLOAD_STATE_FAILED = 5;

    private static SpeechRecognizer recognizer;
    private static SpeechRecognizer modelDownloadRecognizer;
    private static ParcelFileDescriptor readPipe;
    private static int sessionGeneration = 0;
    private static boolean sessionActive = false;
    private static String lastPartialText = "";
    private static String lastFinalText = "";
    private static int modelDownloadState = DOWNLOAD_STATE_IDLE;
    private static int modelDownloadProgress = -1;
    private static String modelDownloadMessage = "";
    private static String modelDownloadLanguageTag = "";

    private LatryTranscriptionManager() {
    }

    public static boolean isTranscriptionAvailable(Context context) {
        return queryRecognitionSupport(context).ready;
    }

    public static String getTranscriptionSupportJson(Context context) {
        final Context appContext = context != null ? context.getApplicationContext() : null;
        return buildTranscriptionSupportJson(appContext);
    }

    public static boolean requestTranscriptionModelDownload(Context context) {
        return requestTranscriptionModelDownload(context, null);
    }

    public static boolean requestTranscriptionModelDownload(Context context, String languageTag) {
        final Context appContext = context != null ? context.getApplicationContext() : null;
        final String normalizedLanguageTag = normalizeLanguageTag(languageTag);
        final SupportCheckResult support = queryRecognitionSupport(appContext);
        if (normalizedLanguageTag != null) {
            if (support.installedLanguages.contains(normalizedLanguageTag)) {
                setCachedDownloadState(DOWNLOAD_STATE_READY, 100,
                        "On-device speech model is already installed for "
                                + normalizedLanguageTag,
                        normalizedLanguageTag);
                return true;
            }

            if (support.pendingLanguages.contains(normalizedLanguageTag)) {
                setCachedDownloadState(DOWNLOAD_STATE_SCHEDULED, -1,
                        "Android is still preparing the on-device speech model for "
                                + normalizedLanguageTag,
                        normalizedLanguageTag);
                return true;
            }

            if (!support.supportedLanguages.contains(normalizedLanguageTag)) {
                setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                        "Android does not report an on-device speech model for "
                                + normalizedLanguageTag,
                        normalizedLanguageTag);
                return false;
            }
        } else {
            if (support.ready) {
                setCachedDownloadState(DOWNLOAD_STATE_READY, 100,
                        "On-device speech recognition is already ready",
                        "");
                return true;
            }

            if (!support.canDownload) {
                setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1, support.message, "");
                return false;
            }
        }

        return runOnMainSync(() -> requestTranscriptionModelDownloadOnMain(appContext,
                normalizedLanguageTag), false);
    }

    public static boolean openTranscriptionSettings(Context context) {
        final Context appContext = context != null ? context.getApplicationContext() : null;
        if (appContext == null) {
            return false;
        }

        final Intent intent = new Intent(Settings.ACTION_VOICE_INPUT_SETTINGS);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            appContext.startActivity(intent);
            return true;
        } catch (Throwable primaryFailure) {
            Log.w(TAG, "Failed to open voice input settings", primaryFailure);
        }

        final Intent fallbackIntent = new Intent(Settings.ACTION_SETTINGS);
        fallbackIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            appContext.startActivity(fallbackIntent);
            return true;
        } catch (Throwable fallbackFailure) {
            Log.w(TAG, "Failed to open Android settings fallback", fallbackFailure);
            return false;
        }
    }

    public static int createPipeAndStart(Context context) {
        final Context appContext = context != null ? context.getApplicationContext() : null;
        final SupportCheckResult support = queryRecognitionSupport(appContext);
        if (!support.ready) {
            notifyTranscriptionError(support.errorCode, support.message);
            return -1;
        }

        return runOnMainSync(() -> createPipeAndStartOnMain(appContext,
                support.installedLanguages), -1);
    }

    public static void stopTranscription() {
        runOnMainAsync(() -> teardownSession(true));
    }

    public static void destroy() {
        runOnMainAsync(() -> {
            teardownSession(false);
            destroyModelDownloadRecognizerOnMain();
        });
    }

    private static int createPipeAndStartOnMain(Context context,
                                                List<String> installedLanguages) {
        if (!hasRecordAudioPermission(context)) {
            notifyTranscriptionError(SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS,
                    "RECORD_AUDIO permission is required for Android speech recognition");
            return -1;
        }

        teardownSession(false);

        final SpeechRecognizer createdRecognizer;
        try {
            createdRecognizer = SpeechRecognizer.createOnDeviceSpeechRecognizer(context);
        } catch (Throwable throwable) {
            Log.w(TAG, "Failed to create on-device SpeechRecognizer", throwable);
            notifyTranscriptionError(SpeechRecognizer.ERROR_CLIENT,
                    "Failed to create on-device SpeechRecognizer");
            return -1;
        }

        if (createdRecognizer == null) {
            notifyTranscriptionError(SpeechRecognizer.ERROR_CLIENT,
                    "On-device SpeechRecognizer is unavailable");
            return -1;
        }

        final ParcelFileDescriptor[] pipe;
        try {
            pipe = ParcelFileDescriptor.createPipe();
        } catch (IOException exception) {
            createdRecognizer.destroy();
            Log.w(TAG, "Failed to create transcription pipe", exception);
            notifyTranscriptionError(SpeechRecognizer.ERROR_CLIENT,
                    "Failed to create transcription pipe");
            return -1;
        }

        final int generation = ++sessionGeneration;
        recognizer = createdRecognizer;
        readPipe = pipe[0];
        sessionActive = true;
        lastPartialText = "";
        lastFinalText = "";
        recognizer.setRecognitionListener(new LiveRecognitionListener(generation));

        final int writeFd = pipe[1].detachFd();
        try {
            recognizer.startListening(buildRecognitionIntent(readPipe, installedLanguages));
            return writeFd;
        } catch (Throwable throwable) {
            Log.w(TAG, "Failed to start on-device recognition session", throwable);
            closeDetachedFdQuietly(writeFd);
            teardownSession(false);
            notifyTranscriptionError(SpeechRecognizer.ERROR_CLIENT,
                    "Failed to start on-device recognition session");
            return -1;
        }
    }

    private static Intent buildRecognitionIntent(ParcelFileDescriptor audioSource,
                                                 List<String> installedLanguages) {
        final Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        intent.putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE, audioSource);
        intent.putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_ENCODING,
                AudioFormat.ENCODING_PCM_16BIT);
        intent.putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_SAMPLING_RATE, 16000);
        intent.putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_CHANNEL_COUNT, 1);
        intent.putExtra(RecognizerIntent.EXTRA_SEGMENTED_SESSION,
                RecognizerIntent.EXTRA_AUDIO_SOURCE);
        intent.putExtra(RecognizerIntent.EXTRA_ENABLE_FORMATTING,
                RecognizerIntent.FORMATTING_OPTIMIZE_LATENCY);
        intent.putExtra(RecognizerIntent.EXTRA_HIDE_PARTIAL_TRAILING_PUNCTUATION, true);

        final ArrayList<String> normalizedInstalledLanguages =
                installedLanguages == null ? new ArrayList<>() : new ArrayList<>(installedLanguages);
        if (normalizedInstalledLanguages.size() == 1) {
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE,
                    normalizedInstalledLanguages.get(0));
        } else if (normalizedInstalledLanguages.size() > 1) {
            intent.putExtra(RecognizerIntent.EXTRA_ENABLE_LANGUAGE_DETECTION, true);
            intent.putExtra(RecognizerIntent.EXTRA_ENABLE_LANGUAGE_SWITCH,
                    RecognizerIntent.LANGUAGE_SWITCH_BALANCED);
            intent.putStringArrayListExtra(
                    RecognizerIntent.EXTRA_LANGUAGE_DETECTION_ALLOWED_LANGUAGES,
                    normalizedInstalledLanguages);
            intent.putStringArrayListExtra(
                    RecognizerIntent.EXTRA_LANGUAGE_SWITCH_ALLOWED_LANGUAGES,
                    normalizedInstalledLanguages);
        }

        return intent;
    }

    private static Intent buildSupportCheckIntent() {
        return buildSupportCheckIntent(null);
    }

    private static Intent buildSupportCheckIntent(String languageTag) {
        final Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        intent.putExtra(RecognizerIntent.EXTRA_ENABLE_FORMATTING,
                RecognizerIntent.FORMATTING_OPTIMIZE_LATENCY);
        intent.putExtra(RecognizerIntent.EXTRA_HIDE_PARTIAL_TRAILING_PUNCTUATION, true);
        final String normalizedLanguageTag = normalizeLanguageTag(languageTag);
        if (normalizedLanguageTag != null) {
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, normalizedLanguageTag);
        } else {
            intent.putExtra(RecognizerIntent.EXTRA_ENABLE_LANGUAGE_DETECTION, true);
            intent.putExtra(RecognizerIntent.EXTRA_ENABLE_LANGUAGE_SWITCH,
                    RecognizerIntent.LANGUAGE_SWITCH_BALANCED);
        }
        return intent;
    }

    private static String buildTranscriptionSupportJson(Context context) {
        final CachedDownloadState cached = getCachedDownloadState();
        final SupportCheckResult support = queryRecognitionSupport(context);

        try {
            return buildTranscriptionSupportObject(support, cached).toString();
        } catch (JSONException exception) {
            Log.w(TAG, "Failed to encode transcription support state", exception);
            return "{\"available\":false,\"canDownload\":false,"
                    + "\"downloadInProgress\":false,\"downloadProgress\":-1,"
                    + "\"statusMessage\":\"Failed to encode transcription support state\"}";
        }
    }

    private static JSONObject buildTranscriptionSupportObject(SupportCheckResult support,
                                                              CachedDownloadState cached)
            throws JSONException {
        final JSONObject object = new JSONObject();
        final boolean available = support.ready || cached.state == DOWNLOAD_STATE_READY;
        final boolean downloadInProgress = cached.state == DOWNLOAD_STATE_REQUESTED
                || cached.state == DOWNLOAD_STATE_IN_PROGRESS
                || cached.state == DOWNLOAD_STATE_SCHEDULED
                || !support.pendingLanguages.isEmpty();
        final boolean canDownload = !available
                && (support.canDownload || downloadInProgress);

        String statusMessage = support.message;
        if (available) {
            statusMessage = cached.message.isEmpty()
                    ? "On-device speech recognition is ready"
                    : cached.message;
        } else if (!cached.message.isEmpty()
                && (downloadInProgress || cached.state == DOWNLOAD_STATE_FAILED)) {
            statusMessage = cached.message;
        }

        object.put("available", available);
        object.put("canDownload", canDownload);
        object.put("downloadInProgress", downloadInProgress);
        object.put("downloadProgress", cached.progress);
        object.put("statusMessage", statusMessage);
        object.put("downloadTargetLanguage", cached.languageTag);
        object.put("errorCode", support.errorCode);
        object.put("installedLanguages", new JSONArray(support.installedLanguages));
        object.put("pendingLanguages", new JSONArray(support.pendingLanguages));
        object.put("supportedLanguages", new JSONArray(support.supportedLanguages));
        return object;
    }

    private static boolean requestTranscriptionModelDownloadOnMain(Context context,
                                                                  String languageTag) {
        if (context == null) {
            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "Android context is unavailable",
                    normalizeLanguageTag(languageTag));
            return false;
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "Android 14 or newer is required for speech model downloads",
                    normalizeLanguageTag(languageTag));
            return false;
        }

        final CachedDownloadState cached = getCachedDownloadState();
        final String normalizedLanguageTag = normalizeLanguageTag(languageTag);
        final String targetSuffix = normalizedLanguageTag == null
                ? ""
                : " for " + normalizedLanguageTag;
        if (cached.state == DOWNLOAD_STATE_REQUESTED
                || cached.state == DOWNLOAD_STATE_IN_PROGRESS
                || cached.state == DOWNLOAD_STATE_SCHEDULED) {
            if (cached.languageTag.equals(normalizedLanguageTag)) {
                return true;
            }

            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "Another speech model download is already in progress",
                    cached.languageTag);
            return false;
        }

        destroyModelDownloadRecognizerOnMain();

        final SpeechRecognizer createdRecognizer;
        try {
            createdRecognizer = SpeechRecognizer.createOnDeviceSpeechRecognizer(context);
        } catch (Throwable throwable) {
            Log.w(TAG, "Failed to create SpeechRecognizer for model download", throwable);
            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "Failed to create on-device SpeechRecognizer",
                    normalizedLanguageTag);
            return false;
        }

        if (createdRecognizer == null) {
            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "On-device SpeechRecognizer is unavailable",
                    normalizedLanguageTag);
            return false;
        }

        modelDownloadRecognizer = createdRecognizer;
        setCachedDownloadState(DOWNLOAD_STATE_REQUESTED, 0,
                "Requesting on-device speech model download" + targetSuffix,
                normalizedLanguageTag);

        try {
            createdRecognizer.triggerModelDownload(
                    buildSupportCheckIntent(normalizedLanguageTag),
                    context.getMainExecutor(),
                    new ModelDownloadListener() {
                        @Override
                        public void onProgress(int completedPercent) {
                            setCachedDownloadState(DOWNLOAD_STATE_IN_PROGRESS,
                                    completedPercent,
                                    "Downloading on-device speech model ("
                                            + completedPercent + "%)"
                                            + targetSuffix,
                                    normalizedLanguageTag);
                        }

                        @Override
                        public void onScheduled() {
                            setCachedDownloadState(DOWNLOAD_STATE_SCHEDULED, -1,
                                    "Android scheduled the on-device speech model download"
                                            + targetSuffix,
                                    normalizedLanguageTag);
                            destroyModelDownloadRecognizerOnMain();
                        }

                        @Override
                        public void onSuccess() {
                            setCachedDownloadState(DOWNLOAD_STATE_READY, 100,
                                    "On-device speech model download complete"
                                            + targetSuffix,
                                    normalizedLanguageTag);
                            destroyModelDownloadRecognizerOnMain();
                        }

                        @Override
                        public void onError(int error) {
                            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                                    describeErrorCode(error) + targetSuffix,
                                    normalizedLanguageTag);
                            destroyModelDownloadRecognizerOnMain();
                        }
                    });
            return true;
        } catch (Throwable throwable) {
            Log.w(TAG, "Failed to request on-device speech model download", throwable);
            setCachedDownloadState(DOWNLOAD_STATE_FAILED, -1,
                    "Failed to request on-device speech model download"
                            + targetSuffix,
                    normalizedLanguageTag);
            destroyModelDownloadRecognizerOnMain();
            return false;
        }
    }

    private static void teardownSession(boolean notifyStopped) {
        final boolean hadSession = sessionActive || recognizer != null || readPipe != null;
        sessionActive = false;
        sessionGeneration++;
        lastPartialText = "";
        lastFinalText = "";

        if (recognizer != null) {
            try {
                recognizer.cancel();
            } catch (Throwable throwable) {
                Log.d(TAG, "SpeechRecognizer cancel() ignored during teardown", throwable);
            }

            try {
                recognizer.destroy();
            } catch (Throwable throwable) {
                Log.d(TAG, "SpeechRecognizer destroy() ignored during teardown", throwable);
            }
            recognizer = null;
        }

        closeReadPipeQuietly();

        if (notifyStopped && hadSession) {
            notifyTranscriptionStopped();
        }
    }

    private static boolean hasRecordAudioPermission(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }

        return context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    }

    private static boolean isCurrentSession(int generation) {
        return sessionActive && generation == sessionGeneration;
    }

    private static void dispatchPartialResult(int generation, Bundle results) {
        if (!isCurrentSession(generation)) {
            return;
        }

        final String text = normalizeText(extractBestText(results));
        if (text.equals(lastPartialText)) {
            return;
        }

        lastPartialText = text;
        notifyPartialTranscription(text);
    }

    private static void dispatchFinalResult(int generation, Bundle results) {
        if (!isCurrentSession(generation)) {
            return;
        }

        final String text = normalizeText(extractBestText(results));
        lastPartialText = "";
        if (text.isEmpty() || text.equals(lastFinalText)) {
            return;
        }

        lastFinalText = text;
        notifyFinalTranscription(text);
    }

    private static void handleRecognitionError(int generation, int errorCode) {
        if (!isCurrentSession(generation)) {
            return;
        }

        final String message = describeErrorCode(errorCode);
        Log.w(TAG, "SpeechRecognizer error " + errorCode + ": " + message);
        teardownSession(false);
        notifyTranscriptionError(errorCode, message);
        notifyTranscriptionStopped();
    }

    private static void handleSegmentedSessionEnded(int generation) {
        if (!isCurrentSession(generation)) {
            return;
        }

        Log.d(TAG, "Segmented recognition session ended");
        teardownSession(true);
    }

    private static String extractBestText(Bundle results) {
        if (results == null) {
            return "";
        }

        final ArrayList<String> hypotheses =
                results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        if (hypotheses == null || hypotheses.isEmpty() || hypotheses.get(0) == null) {
            return "";
        }

        return hypotheses.get(0);
    }

    private static String normalizeText(String text) {
        if (text == null) {
            return "";
        }

        return text.trim().replaceAll("\\s+", " ");
    }

    private static String describeErrorCode(int errorCode) {
        switch (errorCode) {
            case SpeechRecognizer.ERROR_AUDIO:
                return "Audio input error";
            case SpeechRecognizer.ERROR_CANNOT_CHECK_SUPPORT:
                return "Cannot check speech recognizer support";
            case SpeechRecognizer.ERROR_CANNOT_LISTEN_TO_DOWNLOAD_EVENTS:
                return "Speech recognizer download events unavailable";
            case SpeechRecognizer.ERROR_CLIENT:
                return "Client error";
            case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS:
                return "Insufficient permissions";
            case SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED:
                return "Language not supported";
            case SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE:
                return "Language model unavailable on device";
            case SpeechRecognizer.ERROR_NETWORK:
                return "Network error";
            case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
                return "Network timeout";
            case SpeechRecognizer.ERROR_NO_MATCH:
                return "No speech match";
            case SpeechRecognizer.ERROR_RECOGNIZER_BUSY:
                return "Speech recognizer busy";
            case SpeechRecognizer.ERROR_SERVER:
                return "Recognizer server error";
            case SpeechRecognizer.ERROR_SERVER_DISCONNECTED:
                return "Recognizer service disconnected";
            case SpeechRecognizer.ERROR_SPEECH_TIMEOUT:
                return "Speech timeout";
            case SpeechRecognizer.ERROR_TOO_MANY_REQUESTS:
                return "Too many recognition requests";
            default:
                return "Speech recognition error";
        }
    }

    private static SupportCheckResult queryRecognitionSupport(Context context) {
        if (context == null) {
            return SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                    "Android context is unavailable",
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            return SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                    "Android 14 or newer is required for live transcription",
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        try {
            if (!SpeechRecognizer.isOnDeviceRecognitionAvailable(context)) {
                return SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                        "On-device speech recognition is unavailable on this device",
                        Collections.emptyList(),
                        Collections.emptyList(),
                        Collections.emptyList());
            }
        } catch (Throwable throwable) {
            Log.w(TAG, "Failed to query on-device recognition availability", throwable);
            return SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                    "Failed to query on-device speech recognizer availability",
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        if (Looper.myLooper() == Looper.getMainLooper()) {
            Log.w(TAG, "Recognition support check requested on Android main thread");
            return SupportCheckResult.error(SpeechRecognizer.ERROR_CANNOT_CHECK_SUPPORT,
                    "Recognition support check must run off the Android main thread",
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicReference<SupportCheckResult> result = new AtomicReference<>(
                SupportCheckResult.error(SpeechRecognizer.ERROR_CANNOT_CHECK_SUPPORT,
                        "Timed out while checking on-device speech model support",
                        Collections.emptyList(),
                        Collections.emptyList(),
                        Collections.emptyList()));
        final AtomicReference<SpeechRecognizer> pendingRecognizer = new AtomicReference<>();
        final Context appContext = context.getApplicationContext();

        MAIN_HANDLER.post(() -> {
            SpeechRecognizer supportRecognizer = null;
            try {
                supportRecognizer = SpeechRecognizer.createOnDeviceSpeechRecognizer(appContext);
                if (supportRecognizer == null) {
                    result.set(SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                            "On-device SpeechRecognizer is unavailable",
                            Collections.emptyList(),
                            Collections.emptyList(),
                            Collections.emptyList()));
                    latch.countDown();
                    return;
                }

                pendingRecognizer.set(supportRecognizer);
                final SpeechRecognizer finalRecognizer = supportRecognizer;
                supportRecognizer.checkRecognitionSupport(
                        buildSupportCheckIntent(),
                        command -> command.run(),
                        new RecognitionSupportCallback() {
                            @Override
                            public void onSupportResult(RecognitionSupport support) {
                                result.set(resolveSupportResult(support));
                                pendingRecognizer.compareAndSet(finalRecognizer, null);
                                destroyRecognizerQuietly(finalRecognizer);
                                latch.countDown();
                            }

                            @Override
                            public void onError(int error) {
                                result.set(SupportCheckResult.error(error,
                                        describeErrorCode(error),
                                        Collections.emptyList(),
                                        Collections.emptyList(),
                                        Collections.emptyList()));
                                pendingRecognizer.compareAndSet(finalRecognizer, null);
                                destroyRecognizerQuietly(finalRecognizer);
                                latch.countDown();
                            }
                        });
                return;
            } catch (Throwable throwable) {
                Log.w(TAG, "Failed to check on-device recognition support", throwable);
                result.set(SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                        "Failed to check on-device speech recognizer support",
                        Collections.emptyList(),
                        Collections.emptyList(),
                        Collections.emptyList()));
            }

            if (supportRecognizer != null) {
                pendingRecognizer.compareAndSet(supportRecognizer, null);
                destroyRecognizerQuietly(supportRecognizer);
            }
            latch.countDown();
        });

        try {
            if (!latch.await(SUPPORT_CHECK_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                final SpeechRecognizer timedOutRecognizer = pendingRecognizer.getAndSet(null);
                if (timedOutRecognizer != null) {
                    destroyRecognizerQuietly(timedOutRecognizer);
                }
                Log.w(TAG, "Timed out while checking on-device recognition support");
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            final SpeechRecognizer interruptedRecognizer = pendingRecognizer.getAndSet(null);
            if (interruptedRecognizer != null) {
                destroyRecognizerQuietly(interruptedRecognizer);
            }
            Log.w(TAG, "Interrupted while waiting for recognition support check", exception);
            return SupportCheckResult.error(SpeechRecognizer.ERROR_CLIENT,
                    "Interrupted while checking on-device speech recognizer support",
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        return result.get();
    }

    private static SupportCheckResult resolveSupportResult(RecognitionSupport support) {
        final List<String> installed = support.getInstalledOnDeviceLanguages();
        final List<String> pending = support.getPendingOnDeviceLanguages();
        final List<String> supported = support.getSupportedOnDeviceLanguages();

        Log.d(TAG, "Recognition support: installed=" + installed
                + " pending=" + pending + " supported=" + supported);

        if (!installed.isEmpty()) {
            return SupportCheckResult.ready(installed, pending, supported);
        }

        if (!pending.isEmpty()) {
            return SupportCheckResult.error(SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE,
                    "Android is still preparing the on-device speech model download",
                    installed,
                    pending,
                    supported);
        }

        if (!supported.isEmpty()) {
            return SupportCheckResult.error(SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE,
                    "On-device speech model is available to download",
                    installed,
                    pending,
                    supported);
        }

        return SupportCheckResult.error(SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE,
                "No installed or downloadable on-device speech recognition languages are available",
                installed,
                pending,
                supported);
    }

    private static void closeReadPipeQuietly() {
        if (readPipe == null) {
            return;
        }

        try {
            readPipe.close();
        } catch (IOException exception) {
            Log.d(TAG, "Ignoring read pipe close failure", exception);
        }
        readPipe = null;
    }

    private static void closeDetachedFdQuietly(int fd) {
        if (fd < 0) {
            return;
        }

        try {
            ParcelFileDescriptor.adoptFd(fd).close();
        } catch (IOException exception) {
            Log.d(TAG, "Ignoring detached fd close failure", exception);
        }
    }

    private static void destroyRecognizerQuietly(SpeechRecognizer speechRecognizer) {
        if (speechRecognizer == null) {
            return;
        }

        final Runnable destroyTask = () -> {
            try {
                speechRecognizer.destroy();
            } catch (Throwable throwable) {
                Log.d(TAG, "Ignoring support recognizer destroy failure", throwable);
            }
        };

        if (Looper.myLooper() == Looper.getMainLooper()) {
            destroyTask.run();
        } else {
            MAIN_HANDLER.post(destroyTask);
        }
    }

    private static void destroyModelDownloadRecognizerOnMain() {
        if (modelDownloadRecognizer == null) {
            return;
        }

        final SpeechRecognizer speechRecognizer = modelDownloadRecognizer;
        modelDownloadRecognizer = null;
        try {
            speechRecognizer.destroy();
        } catch (Throwable throwable) {
            Log.d(TAG, "Ignoring model download recognizer destroy failure", throwable);
        }
    }

    private static CachedDownloadState getCachedDownloadState() {
        synchronized (DOWNLOAD_STATE_LOCK) {
            return new CachedDownloadState(modelDownloadState,
                    modelDownloadProgress,
                    modelDownloadMessage,
                    modelDownloadLanguageTag);
        }
    }

    private static void setCachedDownloadState(int state, int progress, String message) {
        setCachedDownloadState(state, progress, message, "");
    }

    private static void setCachedDownloadState(int state,
                                               int progress,
                                               String message,
                                               String languageTag) {
        synchronized (DOWNLOAD_STATE_LOCK) {
            modelDownloadState = state;
            modelDownloadProgress = progress;
            modelDownloadMessage = message != null ? message : "";
            modelDownloadLanguageTag = languageTag != null ? languageTag : "";
        }
    }

    private static String normalizeLanguageTag(String languageTag) {
        if (languageTag == null) {
            return null;
        }

        final String trimmed = languageTag.trim();
        return trimmed.isEmpty() ? null : trimmed;
    }

    private static void runOnMainAsync(Runnable runnable) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            runnable.run();
        } else {
            MAIN_HANDLER.post(runnable);
        }
    }

    private static <T> T runOnMainSync(MainThreadCallable<T> callable, T fallbackValue) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return callable.call();
        }

        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicReference<T> result = new AtomicReference<>(fallbackValue);
        MAIN_HANDLER.post(() -> {
            try {
                result.set(callable.call());
            } finally {
                latch.countDown();
            }
        });

        try {
            latch.await();
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            Log.w(TAG, "Interrupted while waiting for Android main thread", exception);
            return fallbackValue;
        }

        return result.get();
    }

    private interface MainThreadCallable<T> {
        T call();
    }

    private static final class CachedDownloadState {
        private final int state;
        private final int progress;
        private final String message;
        private final String languageTag;

        private CachedDownloadState(int state,
                                    int progress,
                                    String message,
                                    String languageTag) {
            this.state = state;
            this.progress = progress;
            this.message = message;
            this.languageTag = languageTag != null ? languageTag : "";
        }
    }

    private static final class SupportCheckResult {
        private final boolean ready;
        private final boolean canDownload;
        private final int errorCode;
        private final String message;
        private final List<String> installedLanguages;
        private final List<String> pendingLanguages;
        private final List<String> supportedLanguages;

        private SupportCheckResult(boolean ready,
                                   boolean canDownload,
                                   int errorCode,
                                   String message,
                                   List<String> installedLanguages,
                                   List<String> pendingLanguages,
                                   List<String> supportedLanguages) {
            this.ready = ready;
            this.canDownload = canDownload;
            this.errorCode = errorCode;
            this.message = message;
            this.installedLanguages = installedLanguages;
            this.pendingLanguages = pendingLanguages;
            this.supportedLanguages = supportedLanguages;
        }

        private static SupportCheckResult ready(List<String> installedLanguages,
                                                List<String> pendingLanguages,
                                                List<String> supportedLanguages) {
            return new SupportCheckResult(true,
                    false,
                    0,
                    "On-device speech recognition is ready",
                    copyLanguages(installedLanguages),
                    copyLanguages(pendingLanguages),
                    copyLanguages(supportedLanguages));
        }

        private static SupportCheckResult pending(String message, int progress) {
            final String normalizedMessage = message != null && !message.isEmpty()
                    ? message
                    : "Downloading on-device speech model";
            return new SupportCheckResult(false,
                    true,
                    SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE,
                    normalizedMessage,
                    Collections.emptyList(),
                    Collections.emptyList(),
                    Collections.emptyList());
        }

        private static SupportCheckResult error(int errorCode,
                                                String message,
                                                List<String> installedLanguages,
                                                List<String> pendingLanguages,
                                                List<String> supportedLanguages) {
            return new SupportCheckResult(false,
                    !pendingLanguages.isEmpty() || !supportedLanguages.isEmpty(),
                    errorCode,
                    message,
                    copyLanguages(installedLanguages),
                    copyLanguages(pendingLanguages),
                    copyLanguages(supportedLanguages));
        }

        private static List<String> copyLanguages(List<String> languages) {
            if (languages == null || languages.isEmpty()) {
                return Collections.emptyList();
            }

            return new ArrayList<>(languages);
        }
    }

    private static final class LiveRecognitionListener implements RecognitionListener {
        private final int generation;

        LiveRecognitionListener(int generation) {
            this.generation = generation;
        }

        @Override
        public void onReadyForSpeech(Bundle params) {
        }

        @Override
        public void onBeginningOfSpeech() {
        }

        @Override
        public void onRmsChanged(float rmsdB) {
        }

        @Override
        public void onBufferReceived(byte[] buffer) {
        }

        @Override
        public void onEndOfSpeech() {
        }

        @Override
        public void onError(int error) {
            handleRecognitionError(generation, error);
        }

        @Override
        public void onResults(Bundle results) {
            dispatchFinalResult(generation, results);
        }

        @Override
        public void onPartialResults(Bundle partialResults) {
            dispatchPartialResult(generation, partialResults);
        }

        @Override
        public void onEvent(int eventType, Bundle params) {
        }

        @Override
        public void onSegmentResults(Bundle segmentResults) {
            dispatchFinalResult(generation, segmentResults);
        }

        @Override
        public void onEndOfSegmentedSession() {
            handleSegmentedSessionEnded(generation);
        }

        @Override
        public void onLanguageDetection(Bundle results) {
            if (!isCurrentSession(generation) || results == null) {
                return;
            }

            final String detectedLanguage =
                    results.getString(SpeechRecognizer.DETECTED_LANGUAGE);
            if (detectedLanguage != null && !detectedLanguage.isEmpty()) {
                Log.d(TAG, "Detected language: " + detectedLanguage);
            }
        }
    }

    private static native void notifyPartialTranscription(String text);
    private static native void notifyFinalTranscription(String text);
    private static native void notifyTranscriptionError(int errorCode, String message);
    private static native void notifyTranscriptionStopped();
}
