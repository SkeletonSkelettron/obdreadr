package io.github.skeletonskelettron.obdreadr;

import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

// Android does not let an app open itself from the background, and an
// ELM327 does not announce itself when it powers up. What does announce
// itself is the car's own Bluetooth (the stereo) connecting to the phone:
// when it is the device the user picked, this posts a notification that
// opens the app, which then connects by itself.
public class CarBluetoothReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!BluetoothDevice.ACTION_ACL_CONNECTED.equals(intent.getAction()))
            return;
        BluetoothDevice device;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice.class);
        else
            device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
        if (device == null || ObdService.running)
            return;
        String wanted = ObdBackground.reminderDevice(context);
        try {
            if (!wanted.isEmpty() && wanted.equalsIgnoreCase(device.getAddress()))
                ObdBackground.remind(context);
        } catch (SecurityException e) {
            // No Bluetooth permission: nothing to compare.
        }
    }
}
