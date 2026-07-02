package yo6say.latry;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;

public final class LatryMediaSessionManager {
    public interface ControlEventListener {
        void onControlEvent(int eventType);
    }

    public static final int EVENT_PTT_PRESS = 1;
    public static final int EVENT_PTT_RELEASE = 2;
    public static final int EVENT_MEDIA_PLAY = 3;
    public static final int EVENT_MEDIA_PAUSE = 4;
    public static final int EVENT_MEDIA_STOP = 5;

    private static final String TAG = "LatryMediaSession";
    private static final String SESSION_TAG = "LatryRadio";
    private static final String ACTION_PTT_PRESS = "yo6say.latry.action.PTT_PRESS";
    private static final String ACTION_PTT_RELEASE = "yo6say.latry.action.PTT_RELEASE";

    private final Context context;
    private final ControlEventListener controlEventListener;

    private MediaSession mediaSession;
    private String currentCallsign = "";
    private int currentTalkgroup = 0;
    private String currentTalker = "";
    private boolean isConnected = false;
    private boolean isReceiving = false;
    private boolean isTransmitting = false;

    public LatryMediaSessionManager(Context context, ControlEventListener controlEventListener) {
        this.context = context.getApplicationContext();
        this.controlEventListener = controlEventListener;
    }

    public void initialize() {
        if (mediaSession != null) {
            Log.i(TAG, "Media session already initialized");
            return;
        }

        Log.i(TAG, "Initializing media session for Android Auto style controls");

        mediaSession = new MediaSession(context, SESSION_TAG);

        Intent openAppIntent = new Intent(context, LatryActivity.class);
        openAppIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent sessionActivity = PendingIntent.getActivity(
                context,
                0,
                openAppIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        mediaSession.setSessionActivity(sessionActivity);

        mediaSession.setCallback(new MediaSession.Callback() {
            @Override
            public void onPlay() {
                Log.i(TAG, "Play requested from media session");
                dispatchControlEvent(EVENT_MEDIA_PLAY);
            }

            @Override
            public void onPause() {
                Log.i(TAG, "Pause requested from media session");
                dispatchControlEvent(EVENT_MEDIA_PAUSE);
            }

            @Override
            public void onStop() {
                Log.i(TAG, "Stop requested from media session");
                dispatchControlEvent(EVENT_MEDIA_STOP);
            }

            @Override
            public void onCustomAction(String action, Bundle extras) {
                if (ACTION_PTT_PRESS.equals(action)) {
                    Log.i(TAG, "PTT press requested from media session");
                    dispatchControlEvent(EVENT_PTT_PRESS);
                } else if (ACTION_PTT_RELEASE.equals(action)) {
                    Log.i(TAG, "PTT release requested from media session");
                    dispatchControlEvent(EVENT_PTT_RELEASE);
                } else {
                    super.onCustomAction(action, extras);
                }
            }

            @Override
            public boolean onMediaButtonEvent(Intent mediaButtonIntent) {
                if (mediaButtonIntent == null
                        || !Intent.ACTION_MEDIA_BUTTON.equals(mediaButtonIntent.getAction())) {
                    return super.onMediaButtonEvent(mediaButtonIntent);
                }

                KeyEvent keyEvent = extractMediaButtonEvent(mediaButtonIntent);
                if (keyEvent == null) {
                    return super.onMediaButtonEvent(mediaButtonIntent);
                }

                if (keyEvent.getKeyCode() == KeyEvent.KEYCODE_HEADSETHOOK
                        || keyEvent.getKeyCode() == KeyEvent.KEYCODE_CALL) {
                    if (keyEvent.getAction() == KeyEvent.ACTION_DOWN) {
                        dispatchControlEvent(EVENT_PTT_PRESS);
                        return true;
                    }
                    if (keyEvent.getAction() == KeyEvent.ACTION_UP) {
                        dispatchControlEvent(EVENT_PTT_RELEASE);
                        return true;
                    }
                }

                if (HardwarePttKeyPolicy.shouldHandleKeyEvent(
                        context,
                        Build.BRAND,
                        Build.MANUFACTURER,
                        Build.MODEL,
                        Build.DEVICE,
                        Build.PRODUCT,
                        keyEvent.getAction(),
                        keyEvent.getKeyCode())) {
                    if (keyEvent.getAction() == KeyEvent.ACTION_DOWN) {
                        dispatchControlEvent(EVENT_PTT_PRESS);
                        return true;
                    }
                    if (keyEvent.getAction() == KeyEvent.ACTION_UP) {
                        dispatchControlEvent(EVENT_PTT_RELEASE);
                        return true;
                    }
                }

                if (keyEvent.getAction() != KeyEvent.ACTION_DOWN) {
                    return super.onMediaButtonEvent(mediaButtonIntent);
                }

                switch (keyEvent.getKeyCode()) {
                    case KeyEvent.KEYCODE_MEDIA_PLAY:
                        dispatchControlEvent(EVENT_MEDIA_PLAY);
                        return true;
                    case KeyEvent.KEYCODE_MEDIA_PAUSE:
                        dispatchControlEvent(EVENT_MEDIA_PAUSE);
                        return true;
                    case KeyEvent.KEYCODE_MEDIA_STOP:
                        dispatchControlEvent(EVENT_MEDIA_STOP);
                        return true;
                    default:
                        return super.onMediaButtonEvent(mediaButtonIntent);
                }
            }
        });

        mediaSession.setMetadata(buildMetadata("Idle", "Not connected"));
        mediaSession.setPlaybackState(buildPlaybackState(PlaybackState.STATE_STOPPED));
        mediaSession.setActive(true);
    }

    public void release() {
        if (mediaSession == null) {
            return;
        }

        Log.i(TAG, "Releasing media session");
        mediaSession.setActive(false);
        mediaSession.release();
        mediaSession = null;
    }

    public void updateConnectionStatus(boolean connected, String callsign, int talkgroup) {
        isConnected = connected;
        currentCallsign = callsign == null ? "" : callsign;
        currentTalkgroup = talkgroup;

        if (!connected) {
            isReceiving = false;
            isTransmitting = false;
            currentTalker = "";
        }

        refreshSessionState();
    }

    public void updateTalkgroup(int talkgroup) {
        currentTalkgroup = talkgroup;
        refreshSessionState();
    }

    public void updateCurrentTalker(String talker) {
        currentTalker = talker == null ? "" : talker;
        refreshSessionState();
    }

    public void updateRXStatus(boolean receiving, String talker) {
        isReceiving = receiving;
        if (talker != null && !talker.isEmpty()) {
            currentTalker = talker;
        } else if (!receiving) {
            currentTalker = "";
        }
        refreshSessionState();
    }

    public void updateTXStatus(boolean transmitting) {
        isTransmitting = transmitting;
        if (transmitting) {
            isReceiving = false;
        }
        refreshSessionState();
    }

    MediaController getControllerForTesting() {
        if (mediaSession == null) {
            return null;
        }
        return mediaSession.getController();
    }

    private void refreshSessionState() {
        if (mediaSession == null) {
            return;
        }

        mediaSession.setMetadata(buildMetadata(buildTitle(), buildSubtitle()));
        mediaSession.setPlaybackState(buildPlaybackState(
                isConnected ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_STOPPED));
    }

    private MediaMetadata buildMetadata(String title, String subtitle) {
        return new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, subtitle)
                .putString(MediaMetadata.METADATA_KEY_ALBUM, "Latry")
                .putLong(MediaMetadata.METADATA_KEY_DURATION, -1L)
                .build();
    }

    private PlaybackState buildPlaybackState(int state) {
        PlaybackState.Builder builder = new PlaybackState.Builder()
                .setActions(PlaybackState.ACTION_PLAY
                        | PlaybackState.ACTION_PAUSE
                        | PlaybackState.ACTION_STOP
                        | PlaybackState.ACTION_PLAY_PAUSE)
                .setState(state, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1.0f,
                        SystemClock.elapsedRealtime());

        if (isConnected) {
            PlaybackState.CustomAction customAction;
            if (isTransmitting) {
                customAction = new PlaybackState.CustomAction.Builder(
                        ACTION_PTT_RELEASE,
                        "Release PTT",
                        android.R.drawable.ic_media_pause)
                        .build();
            } else {
                customAction = new PlaybackState.CustomAction.Builder(
                        ACTION_PTT_PRESS,
                        "Push to Talk",
                        android.R.drawable.ic_btn_speak_now)
                        .build();
            }
            builder.addCustomAction(customAction);
        }

        return builder.build();
    }

    private void dispatchControlEvent(int eventType) {
        if (controlEventListener != null) {
            controlEventListener.onControlEvent(eventType);
        }
    }

    private KeyEvent extractMediaButtonEvent(Intent mediaButtonIntent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return mediaButtonIntent.getParcelableExtra(Intent.EXTRA_KEY_EVENT, KeyEvent.class);
        }
        return getParcelableExtraCompat(mediaButtonIntent);
    }

    @SuppressWarnings("deprecation")
    private KeyEvent getParcelableExtraCompat(Intent mediaButtonIntent) {
        return mediaButtonIntent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
    }

    private String buildTitle() {
        if (currentTalkgroup > 0) {
            return "TG " + currentTalkgroup;
        }
        if (!currentCallsign.isEmpty()) {
            return currentCallsign;
        }
        return "Latry";
    }

    private String buildSubtitle() {
        if (!isConnected) {
            if (currentTalkgroup > 0 || !currentCallsign.isEmpty()) {
                return "Ready to connect";
            }
            return "Not connected";
        }
        if (isTransmitting) {
            return "Transmitting";
        }
        if (isReceiving) {
            if (!currentTalker.isEmpty()) {
                return "RX: " + currentTalker;
            }
            return "Receiving";
        }
        return "Monitoring";
    }
}
