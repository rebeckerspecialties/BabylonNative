package com.android.babylonnative.playground;

import android.content.Context;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;
import android.util.Log;
import android.view.Choreographer;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import com.babylonjs.embedding.BabylonNative;

import java.util.concurrent.FutureTask;

/** Off-by-default OpenXR frame-owner experiment for the Android Playground. */
final class OpenXrFrameView extends SurfaceView implements SurfaceHolder.Callback {
    private final HandlerThread thread = new HandlerThread("OpenXRFrame", Process.THREAD_PRIORITY_DISPLAY);
    private final Handler owner;
    private final long runtime;
    // Everything below is accessed only on the owner thread.
    private long view;
    private boolean ready, resumed, scheduled, logged;
    private final Runnable tick = this::frame;
    private final Choreographer.FrameCallback vsync = time -> frame();

    OpenXrFrameView(Context context, long runtime) {
        super(context);
        this.runtime = runtime;
        thread.start();
        owner = new Handler(thread.getLooper());
        getHolder().addCallback(this);
    }

    // Surface destruction must finish detaching before Android reclaims its ANativeWindow.
    private void onOwner(Runnable action) {
        FutureTask<Void> work = new FutureTask<>(action, null);
        if (!owner.post(work)) throw new IllegalStateException("OpenXR frame owner is stopped");
        try {
            work.get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(e);
        } catch (java.util.concurrent.ExecutionException e) {
            throw new IllegalStateException(e.getCause());
        }
    }

    private void cancelFrame() {
        Choreographer.getInstance().removeFrameCallback(vsync);
        owner.removeCallbacks(tick);
        scheduled = false;
    }

    private void schedule(boolean xrPaced) {
        if (view == 0 || !resumed || !ready || scheduled) return;
        scheduled = true;
        if (xrPaced) owner.post(tick);
        else Choreographer.getInstance().postFrameCallback(vsync);
    }

    private void frame() {
        scheduled = false;
        if (view == 0 || !ready || !resumed) return;
        long start = System.nanoTime();
        BabylonNative.viewRenderFrame(view);
        boolean active = BabylonNative.runtimeIsXrActive(runtime);
        if (active && !logged) {
            logged = true;
            Log.i("BabylonNative", "OpenXR dedicated frame owner active, tid=" + Process.myTid());
        }
        // A session can exist without a pending XR rAF. Fall back to display cadence
        // on an unpaced return instead of spinning the host until a script requests work.
        schedule(active && System.nanoTime() - start >= 2_000_000L);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        onOwner(() -> view = BabylonNative.viewAttach(runtime, holder.getSurface()));
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        onOwner(() -> {
            BabylonNative.viewResize(view, width, height);
            ready = true;
            schedule(false);
        });
    }

    private void detach() {
        cancelFrame();
        ready = false;
        if (view != 0) BabylonNative.viewDetach(view);
        view = 0;
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) { onOwner(this::detach); }

    void resumeRuntime() {
        onOwner(() -> { BabylonNative.resume(); resumed = true; schedule(false); });
    }

    void pauseRuntime() {
        onOwner(() -> { resumed = false; cancelFrame(); BabylonNative.pause(); });
    }

    void destroyRuntime() {
        getHolder().removeCallback(this);
        onOwner(() -> { detach(); BabylonNative.runtimeDestroy(runtime); });
        thread.quitSafely();
    }
}
