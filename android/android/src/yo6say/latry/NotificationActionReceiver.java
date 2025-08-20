package yo6say.latry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class NotificationActionReceiver extends BroadcastReceiver {
    private static final String TAG = "NotificationActionReceiver";
    
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        Log.d(TAG, "Received notification action: " + action);
        
        if (action == null) return;
        
        if ("ACTION_DISCONNECT".equals(action)) {
            Log.d(TAG, "Processing disconnect action from notification");
            VoipBackgroundService.stopVoipService(context);
        } else {
            Log.w(TAG, "Unknown notification action: " + action);
        }
    }
}