package com.babylonjs.embedding;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import java.io.IOException;
import java.util.Collections;
import java.util.Set;
import java.util.WeakHashMap;

/** Local packaged media for the Android embedding. All decoder access belongs to its looper. */
public final class PackagedAudio {
    private static final Set<PackagedAudio> live = Collections.newSetFromMap(new WeakHashMap<>());
    private static volatile boolean foreground = true;

    public static synchronized void setForeground(boolean value) {
        if (foreground == value) return;
        foreground = value;
        for (PackagedAudio audio : live) {
            audio.handler.post(audio::applyPlayback);
        }
    }
    public static boolean enabled(Context context) {
        if (context == null) return false;
        try {
            android.os.Bundle metadata = context.getPackageManager()
                .getApplicationInfo(context.getPackageName(), PackageManager.GET_META_DATA).metaData;
            return metadata != null && metadata.getBoolean("com.babylonjs.embedding.PACKAGED_AUDIO", false);
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }
    private final HandlerThread thread = new HandlerThread("Babylon media");
    private final Handler handler;
    private final String name;
    private MediaPlayer player;
    private boolean prepared, requested;
    // 0 loading, 1 playing, 2 paused, 3 ended, -1 failed.
    private volatile int state;
    private volatile String error = "";

    public PackagedAudio(Context context, String source) {
        name = Uri.parse(source).getLastPathSegment();
        thread.start();
        handler = new Handler(thread.getLooper());
        synchronized (PackagedAudio.class) { live.add(this); }
        handler.post(() -> {
            try {
                Uri uri = Uri.parse(source);
                if (!"app".equals(uri.getScheme()) || uri.getPath() == null || !uri.getPath().startsWith("/")
                        || (uri.getAuthority() != null && !uri.getAuthority().isEmpty())) {
                    throw new IOException("Expected an app:/// packaged media URL");
                }
                try (AssetFileDescriptor file = context.getAssets().openFd(uri.getPath().substring(1))) {
                    player = new MediaPlayer();
                    player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_UNKNOWN).build());
                    player.setOnPreparedListener(p -> {
                        prepared = true;
                        applyPlayback();
                    });
                    player.setOnCompletionListener(p -> {
                        requested = false; state = 3;
                        android.util.Log.d("PackagedAudio", "Ended: " + name);
                    });
                    player.setOnErrorListener((p, what, extra) -> {
                        fail(source + ": Android media error " + what + "/" + extra);
                        return true;
                    });
                    player.setDataSource(file.getFileDescriptor(), file.getStartOffset(), file.getLength());
                    player.prepareAsync();
                }
            } catch (IOException | RuntimeException e) {
                fail(source + ": " + e);
            }
        });
    }

    private void fail(String detail) { error = detail; state = -1; }
    private void applyPlayback() {
        if (!prepared || player == null || state < 0 || (state == 3 && !requested)) return;
        try {
            if (requested && foreground) {
                player.start();
                if (state != 1) android.util.Log.d("PackagedAudio", "Playing: " + name);
                state = 1;
            }
            else { if (player.isPlaying()) player.pause(); state = 2; }
        } catch (IllegalStateException e) { fail(e.toString()); }
    }
    public void setPlaying(boolean value) {
        handler.post(() -> { requested = value; applyPlayback(); });
    }
    public int state() { return state; }
    public String error() { return error; }
    public void close() {
        synchronized (PackagedAudio.class) { live.remove(this); }
        handler.post(() -> {
            if (player != null) {
                player.setOnCompletionListener(null);
                player.setOnPreparedListener(null);
                player.setOnErrorListener(null);
                player.release();
                player = null;
            }
            thread.quitSafely();
        });
    }
}
