package dev.quickapp.kit.android;

import android.content.Context;
import android.os.Looper;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.io.File;

public final class QuickAppRuntime {
    private enum State { CREATED, ATTACHED, RUNNING, DESTROYED }

    private final Context context;
    private State state = State.CREATED;
    private FrameLayout runtimeRoot;
    private RuntimeBridge bridge;

    private QuickAppRuntime(Context context) {
        this.context = context.getApplicationContext();
    }

    public static QuickAppRuntime create(Context context) {
        if (context == null) throw new IllegalArgumentException("context is required");
        return new QuickAppRuntime(context);
    }

    public QuickAppResult attachSurface(ViewGroup container) {
        if (!isMainThread()) return QuickAppResult.failed("THREAD_REQUIRED", "attachSurface must run on the main thread");
        if (state == State.DESTROYED) return QuickAppResult.destroyed();
        if (state != State.CREATED || container == null) {
            return QuickAppResult.failed("SURFACE_STATE_INVALID", "Runtime surface is already attached or invalid");
        }
        runtimeRoot = new FrameLayout(container.getContext());
        runtimeRoot.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        container.addView(runtimeRoot);
        float density = runtimeRoot.getResources().getDisplayMetrics().density;
        float widthPx = container.getWidth();
        float heightPx = container.getHeight();
        if (widthPx <= 1 || heightPx <= 1) {
            widthPx = container.getResources().getDisplayMetrics().widthPixels;
            heightPx = container.getResources().getDisplayMetrics().heightPixels;
        }
        float width = Math.max(1, widthPx) / density;
        float height = Math.max(1, heightPx) / density;
        bridge = new RuntimeBridge(runtimeRoot, width, height, null);
        state = State.ATTACHED;
        return QuickAppResult.accepted();
    }

    public QuickAppResult loadRpk(File rpk) {
        if (!isMainThread()) return QuickAppResult.failed("THREAD_REQUIRED", "loadRpk must run on the main thread");
        if (state == State.DESTROYED) return QuickAppResult.destroyed();
        if (state != State.ATTACHED || bridge == null) {
            return QuickAppResult.failed("RUNTIME_STATE_INVALID", "attachSurface must complete before loadRpk");
        }
        QuickAppResult validation = RpkValidator.validate(rpk);
        if (!validation.isSuccess()) return validation;
        bridge.setRpkPath(rpk.getAbsolutePath());
        bridge.start(rpk.getAbsolutePath());
        state = State.RUNNING;
        return QuickAppResult.accepted();
    }

    public QuickAppResult dispatchInput(QuickAppInput input) {
        if (!isMainThread()) return QuickAppResult.failed("THREAD_REQUIRED", "dispatchInput must run on the main thread");
        if (state == State.DESTROYED) return QuickAppResult.destroyed();
        if (state != State.RUNNING || bridge == null || input == null) {
            return QuickAppResult.failed("INPUT_STATE_INVALID", "Runtime is not ready for input");
        }
        return bridge.dispatchInput(input) ? QuickAppResult.accepted()
                : QuickAppResult.failed("INPUT_REJECTED", "Platform rejected input");
    }

    public QuickAppResult updateLifecycle(QuickAppLifecycleState lifecycle) {
        if (!isMainThread()) return QuickAppResult.failed("THREAD_REQUIRED", "updateLifecycle must run on the main thread");
        if (state == State.DESTROYED) return QuickAppResult.destroyed();
        if (state != State.RUNNING || lifecycle == null) {
            return QuickAppResult.failed("LIFECYCLE_STATE_INVALID", "Runtime is not running");
        }
        return bridge.updateLifecycle(lifecycle)
                ? QuickAppResult.accepted()
                : QuickAppResult.unsupported("Core lifecycle control is not exposed by this Runtime spine");
    }

    public QuickAppResult destroy() {
        if (!isMainThread()) return QuickAppResult.failed("THREAD_REQUIRED", "destroy must run on the main thread");
        if (state == State.DESTROYED) return QuickAppResult.completed();
        if (bridge != null) bridge.destroy();
        if (runtimeRoot != null && runtimeRoot.getParent() instanceof ViewGroup) {
            ((ViewGroup) runtimeRoot.getParent()).removeView(runtimeRoot);
        }
        bridge = null;
        runtimeRoot = null;
        state = State.DESTROYED;
        return QuickAppResult.completed();
    }

    private static boolean isMainThread() {
        return Looper.myLooper() == Looper.getMainLooper();
    }
}
