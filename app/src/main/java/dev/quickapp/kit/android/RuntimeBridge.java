package dev.quickapp.kit.android;

import android.app.Activity;
import android.util.Log;
import android.widget.FrameLayout;

import java.util.concurrent.atomic.AtomicBoolean;

final class RuntimeBridge {
    private static final String TAG = "QuickAppKit";

    private final Activity activity;
    private final RuntimeSurfaceHost platform;
    private final AtomicBoolean destroyed = new AtomicBoolean(false);
    private long nativeHandle;

    RuntimeBridge(Activity activity, FrameLayout root, float width, float height) {
        this.activity = activity;
        this.platform = new RuntimeSurfaceHost(root, this::dispatchClick);
        this.nativeHandle = NativeGateway.create(this, width, height);
        if (nativeHandle == 0) {
            throw new IllegalStateException("native Runtime creation failed");
        }
    }

    void start(String rpkPath) {
        NativeGateway.start(nativeHandle, rpkPath);
    }

    void destroy() {
        if (!destroyed.compareAndSet(false, true)) return;
        long handle = nativeHandle;
        if (handle != 0) NativeGateway.destroy(handle);
    }

    private void dispatchClick(String surfaceId, String nodeId, long timestampNs) {
        if (!destroyed.get() && nativeHandle != 0) {
            NativeGateway.dispatchClick(nativeHandle, surfaceId, nodeId, timestampNs);
        }
    }

    @SuppressWarnings("unused")
    private void postCreateSurface(String requestId, String surfaceId) {
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() && platform.createSurface(surfaceId);
            NativeGateway.completeSurface(nativeHandle, requestId, 0, surfaceId,
                    null, null, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface creation failed");
        });
    }

    @SuppressWarnings("unused")
    private void postPresentSurface(
            String requestId, String targetSurfaceId, String sourceSurfaceId, boolean push) {
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() && (push
                    ? platform.presentPush(sourceSurfaceId, targetSurfaceId)
                    : platform.presentRoot(targetSurfaceId));
            NativeGateway.completeSurface(nativeHandle, requestId, 1, targetSurfaceId,
                    sourceSurfaceId, null, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface present failed");
        });
    }

    @SuppressWarnings("unused")
    private void postSetSurfaceVisibility(
            String requestId, String surfaceId, boolean visible) {
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() && platform.setVisible(surfaceId, visible);
            NativeGateway.completeSurface(nativeHandle, requestId, 2, surfaceId,
                    null, null, visible ? 1 : 0, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface visibility failed");
        });
    }

    @SuppressWarnings("unused")
    private void postCloseSurface(
            String requestId, String sourceSurfaceId, String revealSurfaceId) {
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() &&
                    platform.closeAndReveal(sourceSurfaceId, revealSurfaceId);
            NativeGateway.completeSurface(nativeHandle, requestId, 3, sourceSurfaceId,
                    sourceSurfaceId, revealSurfaceId, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface close/reveal failed");
        });
    }

    @SuppressWarnings("unused")
    private void postDestroySurface(String requestId, String surfaceId) {
        activity.runOnUiThread(() -> {
            boolean ok = platform.destroySurface(surfaceId);
            NativeGateway.completeSurface(nativeHandle, requestId, 4, surfaceId,
                    null, null, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface destroy failed");
        });
    }

    @SuppressWarnings("unused")
    private void postMountTransaction(MountTransaction transaction) {
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() && platform.apply(transaction);
            NativeGateway.completeMount(nativeHandle, transaction.surfaceId,
                    transaction.revision, transaction.mountAttemptId,
                    transaction.sourceId, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Mount transaction failed");
        });
    }

    @SuppressWarnings("unused")
    private void onRuntimeStarted(String surfaceId) {
        Log.i(TAG, "android.runtime.started surface=" + surfaceId);
    }

    @SuppressWarnings("unused")
    private void onRuntimeFailed(String errorCode, String message) {
        Log.e(TAG, "android.runtime.failed error=" + errorCode + " message=" + message);
    }

    @SuppressWarnings("unused")
    private void onRuntimeStopped(
            int surfaceCount,
            int nodeCount,
            int handlerCount,
            int pendingCallbackCount,
            int jsResourceCount,
            int coreQueueDepth) {
        activity.runOnUiThread(() -> {
            platform.close();
            Log.i(TAG, "android.runtime.stopped surfaces=" + surfaceCount +
                    " nodes=" + nodeCount + " handlers=" + handlerCount +
                    " pendingCallbacks=" + pendingCallbackCount +
                    " jsResources=" + jsResourceCount +
                    " coreQueue=" + coreQueueDepth +
                    " javaSurfaces=" + platform.surfaceCount() +
                    " javaNodes=" + platform.nodeCount());
            nativeHandle = 0;
        });
    }
}
