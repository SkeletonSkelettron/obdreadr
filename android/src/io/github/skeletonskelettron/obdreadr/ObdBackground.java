package io.github.skeletonskelettron.obdreadr;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationManager;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.view.WindowManager;

// Called from C++ (src/background.cpp) through JNI.
public final class ObdBackground {
    private static final int NOTIFICATION_PERMISSION_REQUEST = 7401;

    private ObdBackground() {}

    public static void start(Context context, String title, String text) {
        askForNotifications(context);
        Intent intent = new Intent(context, ObdService.class)
                .putExtra(ObdService.EXTRA_TITLE, title)
                .putExtra(ObdService.EXTRA_TEXT, text);
        try {
            context.startForegroundService(intent);
        } catch (RuntimeException e) {
            // For example when Android does not allow it from the background.
            Log.w(ObdService.TAG, "Cannot start the service", e);
        }
    }

    public static void update(Context context, String title, String text) {
        if (!ObdService.running)
            return;
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        manager.notify(ObdService.NOTIFICATION_ID, ObdService.build(context, title, text));
    }

    public static void stop(Context context) {
        context.stopService(new Intent(context, ObdService.class));
    }

    public static void keepScreenOn(Context context, final boolean on) {
        if (!(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            if (on)
                activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else
                activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        });
    }

    public static boolean batteryRestricted(Context context) {
        PowerManager power = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        return power != null && !power.isIgnoringBatteryOptimizations(context.getPackageName());
    }

    public static void requestUnrestricted(Context context) {
        Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                Uri.parse("package:" + context.getPackageName()));
        if (!(context instanceof Activity))
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            Intent list = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
            if (!(context instanceof Activity))
                list.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                context.startActivity(list);
            } catch (ActivityNotFoundException ignored) {
                Log.w(ObdService.TAG, "No battery optimization settings on this phone");
            }
        }
    }

    // Android 13 and newer hide the notification without this permission;
    // the service runs either way.
    private static void askForNotifications(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU || !(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        if (activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)
            return;
        activity.runOnUiThread(() -> activity.requestPermissions(
                new String[] {Manifest.permission.POST_NOTIFICATIONS}, NOTIFICATION_PERMISSION_REQUEST));
    }
}
