package io.github.skeletonskelettron.obdreadr;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

// Keeps the app's process in the foreground while it is connected to the
// car, so Android neither freezes nor kills it with the screen off, and holds
// a partial wake lock so the CPU keeps polling. The work itself stays in the
// Qt code; this service only shows the ongoing notification.
public class ObdService extends Service {
    static final String TAG = "ObdService";
    static final int NOTIFICATION_ID = 7;
    static final String EXTRA_TITLE = "title";
    static final String EXTRA_TEXT = "text";
    private static final String CHANNEL_ID = "connection";
    private static final long WAKE_LOCK_MS = 12L * 60 * 60 * 1000;

    static volatile boolean running;
    private PowerManager.WakeLock wakeLock;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String title = intent != null ? intent.getStringExtra(EXTRA_TITLE) : null;
        String text = intent != null ? intent.getStringExtra(EXTRA_TEXT) : null;
        Notification notification = build(this, title, text);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            else
                startForeground(NOTIFICATION_ID, notification);
        } catch (RuntimeException e) {
            Log.w(TAG, "Cannot run in the foreground", e);
            stopSelf();
            return START_NOT_STICKY;
        }
        running = true;
        if (wakeLock == null) {
            PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
            wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "obdreadr:connection");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire(WAKE_LOCK_MS);
        }
        // Not restarted by Android after being killed: without the Qt app
        // there is nothing to keep alive.
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        if (wakeLock != null) {
            if (wakeLock.isHeld())
                wakeLock.release();
            wakeLock = null;
        }
        super.onDestroy();
    }

    static Notification build(Context context, String title, String text) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager.getNotificationChannel(CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Connection to the car", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Shown while OBD Reader reads the car in the background");
            channel.setShowBadge(false);
            manager.createNotificationChannel(channel);
        }

        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        PendingIntent tap = open == null ? null
                : PendingIntent.getActivity(context, 0, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        int icon = context.getResources().getIdentifier("ic_stat_obd", "drawable", context.getPackageName());
        if (icon == 0)
            icon = android.R.drawable.stat_notify_sync_noanim;

        String body = text != null ? text : "";
        Notification.Builder builder = new Notification.Builder(context, CHANNEL_ID)
                .setSmallIcon(icon)
                .setContentTitle(title != null ? title : "OBD Reader")
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setShowWhen(false)
                .setCategory(Notification.CATEGORY_SERVICE);
        if (tap != null)
            builder.setContentIntent(tap);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return builder.build();
    }
}
