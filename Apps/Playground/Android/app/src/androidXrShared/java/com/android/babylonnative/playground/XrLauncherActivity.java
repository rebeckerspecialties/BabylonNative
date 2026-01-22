package com.android.babylonnative.playground;

import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import androidx.activity.ComponentActivity;

/**
 * Launcher that starts {@link XrImmersiveActivity} directly in full-space mode using SceneCore.
 */
public class XrLauncherActivity extends ComponentActivity {
    private static final String TAG = "BabylonXrLauncher";
    private boolean mLaunchedImmersive;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        maybeLaunchImmersive();
    }

    private void maybeLaunchImmersive() {
        if (mLaunchedImmersive) {
            return;
        }

        mLaunchedImmersive = true;

        Log.i(TAG, "Launching immersive activity.");
        Intent immersiveIntent = new Intent(this, XrImmersiveActivity.class);
        immersiveIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        immersiveIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(immersiveIntent);

        finish();
    }
}
