package com.android.babylonnative.playground;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import android.widget.FrameLayout;
import android.view.View;

import com.library.babylonnative.BabylonView;

public class PlaygroundActivity extends Activity implements BabylonView.ViewDelegate {
    BabylonView mView;

    // Activity life
    @Override
    protected void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        setContentView(R.layout.activity_playground);

        FrameLayout container = findViewById(R.id.babylon_container);
        mView = new BabylonView(getApplication(), this);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        container.addView(mView, lp);

        findViewById(R.id.enter_ar_button).setOnClickListener(view -> {
            mView.eval("if (typeof startImmersiveAR === 'function') { startImmersiveAR(); }", "enter-ar.js");
        });
    }

    @Override
    protected void onPause() {
        mView.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        mView.onResume();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        mView.onRequestPermissionsResult(requestCode, permissions, results);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && mView.getVisibility() == View.GONE) {
            mView.setVisibility(View.VISIBLE);
        }
    }

    @Override
    public void onViewReady() {
        mView.loadScript("app:///Scripts/experience.js");
    }
}
