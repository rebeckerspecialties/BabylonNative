package com.android.babylonnative.playground;

import android.os.Bundle;
import android.util.Log;

import androidx.activity.ComponentActivity;

import com.library.babylonnative.BabylonView;
import com.library.babylonnative.xr.XrSessionController;

/**
 * Dedicated activity for Android XR builds that should enter immersive presentation.
 */
public class XrImmersiveActivity extends ComponentActivity implements BabylonView.ViewDelegate {
    private static final String TAG = "BabylonXrImmersive";
    private BabylonView mView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "onCreate");
        XrSessionController.setCurrentActivity(this);
        mView = new BabylonView(this, this);
        setContentView(mView);
    }

    @Override
    protected void onPause() {
        Log.i(TAG, "onPause");
        if (mView != null) {
            mView.onPause();
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.i(TAG, "onResume");
        XrSessionController.setCurrentActivity(this);
        if (mView != null) {
            mView.onResume();
        }
    }

    @Override
    public void onViewReady() {
        Log.i(TAG, "onViewReady");
        if (mView != null) {
            mView.eval("globalThis.BABYLON_NATIVE_AUTO_ENTER_XR = true;", "auto-enter-xr.js");
            mView.loadScript("app:///Scripts/experience.js");
        }
        XrSessionController.notifySessionStarted();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "onDestroy");
        XrSessionController.notifySessionStopped();
        XrSessionController.onActivityDestroyed(this);
        super.onDestroy();
    }
}
