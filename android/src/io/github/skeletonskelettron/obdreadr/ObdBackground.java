package io.github.skeletonskelettron.obdreadr;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.SharedPreferences;
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

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

// Called from C++ (src/background.cpp) through JNI.
public final class ObdBackground {
    private static final int NOTIFICATION_PERMISSION_REQUEST = 7401;
    private static final String ALERT_CHANNEL_ID = "alerts";
    private static final int ALERT_ID_BASE = 100;
    private static final String REMINDER_CHANNEL_ID = "reminder";
    private static final int REMINDER_ID = 50;
    private static final String PREFS = "obdreadr";
    private static final String PREF_REMINDER = "reminderDevice";

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

    // Alerts while driving: a channel of their own with high importance, so
    // they sound and pop up even with the screen off or another app open.
    public static void alert(Context context, String title, String text) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null)
            return;
        if (manager.getNotificationChannel(ALERT_CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(
                    ALERT_CHANNEL_ID, "Alerts while driving", NotificationManager.IMPORTANCE_HIGH);
            channel.setDescription("Overheating, charging, check engine light, engine speed and low fuel");
            manager.createNotificationChannel(channel);
        }
        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        PendingIntent tap = open == null ? null
                : PendingIntent.getActivity(context, 1, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        int icon = context.getResources().getIdentifier("ic_stat_obd", "drawable", context.getPackageName());
        if (icon == 0)
            icon = android.R.drawable.stat_sys_warning;
        String body = text != null ? text : "";
        Notification.Builder builder = new Notification.Builder(context, ALERT_CHANNEL_ID)
                .setSmallIcon(icon)
                .setContentTitle(title)
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setCategory(Notification.CATEGORY_STATUS)
                .setAutoCancel(true);
        if (tap != null)
            builder.setContentIntent(tap);
        // One notification per kind of alert: a repeat replaces the old one.
        int id = ALERT_ID_BASE + Math.abs((title != null ? title : "").hashCode() % 1000);
        manager.notify(id, builder.build());
    }

    // The window's brightness, 0..1, or -1 (BRIGHTNESS_OVERRIDE_NONE) for
    // the user's setting.
    public static void setBrightness(Context context, final float level) {
        if (!(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            WindowManager.LayoutParams params = activity.getWindow().getAttributes();
            params.screenBrightness = level;
            activity.getWindow().setAttributes(params);
        });
    }

    // The car's Bluetooth device that triggers the reminder (an address, or
    // empty for none), kept where CarBluetoothReceiver can read it without Qt.
    public static void setReminderDevice(Context context, String address) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(PREF_REMINDER, address != null ? address : "").apply();
    }

    static String reminderDevice(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(PREF_REMINDER, "");
    }

    // The phone's paired Bluetooth devices as JSON: [{"name": ..., "address": ...}].
    public static String bondedDevices(Context context) {
        JSONArray list = new JSONArray();
        try {
            BluetoothManager manager = context.getSystemService(BluetoothManager.class);
            BluetoothAdapter adapter = manager != null ? manager.getAdapter() : null;
            if (adapter != null) {
                for (BluetoothDevice device : adapter.getBondedDevices()) {
                    JSONObject item = new JSONObject();
                    String name = device.getName();
                    item.put("name", name != null ? name : "");
                    item.put("address", device.getAddress());
                    list.put(item);
                }
            }
        } catch (SecurityException | JSONException e) {
            Log.w(ObdService.TAG, "Cannot list the paired devices", e);
        }
        return list.toString();
    }

    // "Car connected - tap to start reading", opening the app.
    static void remind(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null)
            return;
        if (manager.getNotificationChannel(REMINDER_CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(
                    REMINDER_CHANNEL_ID, "Car connected", NotificationManager.IMPORTANCE_HIGH);
            channel.setDescription("When the car's Bluetooth connects, a tap starts reading the car");
            manager.createNotificationChannel(channel);
        }
        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        if (open == null)
            return;
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent tap = PendingIntent.getActivity(context, 2, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        int icon = context.getResources().getIdentifier("ic_stat_obd", "drawable", context.getPackageName());
        if (icon == 0)
            icon = android.R.drawable.stat_sys_data_bluetooth;
        Notification notification = new Notification.Builder(context, REMINDER_CHANNEL_ID)
                .setSmallIcon(icon)
                .setContentTitle("Car connected")
                .setContentText("Tap to start reading the car")
                .setContentIntent(tap)
                .setAutoCancel(true)
                .setTimeoutAfter(10 * 60 * 1000)
                .build();
        manager.notify(REMINDER_ID, notification);
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
