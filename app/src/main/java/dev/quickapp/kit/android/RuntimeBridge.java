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

    RuntimeBridge(Activity activity, FrameLayout root, float width, float height, String rpkPath) {
        this.activity = activity;
        this.platform = new RuntimeSurfaceHost(root, this::dispatchEvent, rpkPath);
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
        Log.i(TAG, "android.runtime.destroy.begin surfaces=" + platform.surfaceCount() +
                " nodes=" + platform.nodeCount());
        platform.close();
        if (handle != 0) NativeGateway.destroy(handle);
        Log.i(TAG, "android.runtime.destroy.end surfaces=" + platform.surfaceCount() +
                " nodes=" + platform.nodeCount());
    }

    private void dispatchClick(String surfaceId, String nodeId, long timestampNs) {
        dispatchEvent(surfaceId, nodeId, "click", null, "value", 0, false,
                false, false, false, false, 0, 0, 0, false, timestampNs);
    }

    private void dispatchEvent(String surfaceId, String nodeId, String eventType,
                               RuntimeSurfaceHost.EventPayload payload,
                               long timestampNs) {
        dispatchEvent(surfaceId, nodeId, eventType,
                payload == null ? null : payload.stringValue,
                payload == null ? "value" : payload.numberName,
                payload == null ? 0 : payload.numberValue,
                payload != null && payload.hasNumber,
                payload != null && payload.booleanValue,
                payload != null && payload.hasBoolean,
                payload != null && payload.fromUser,
                payload != null && payload.hasFromUser,
                payload != null ? payload.scrollOffset : 0,
                payload != null ? payload.contentSize : 0,
                payload != null ? payload.viewportSize : 0,
                payload != null && payload.hasScrollMetrics,
                timestampNs);
    }

    private void dispatchEvent(String surfaceId, String nodeId, String eventType,
                               String value, String numberName, double number,
                               boolean hasNumber,
                               boolean checked, boolean hasChecked,
                               boolean fromUser, boolean hasFromUser,
                               double scrollOffset, double contentSize,
                               double viewportSize, boolean hasScrollMetrics,
                               long timestampNs) {
        if (!destroyed.get() && nativeHandle != 0) {
            NativeGateway.dispatchEvent(nativeHandle, surfaceId, nodeId, eventType,
                    value, numberName, number, hasNumber, checked, hasChecked, fromUser,
                    hasFromUser, scrollOffset, contentSize, viewportSize, hasScrollMetrics,
                    timestampNs);
        }
    }

    @SuppressWarnings("unused")
    private void postCreateSurface(String requestId, String surfaceId) {
        Log.i(TAG, "android.platform.create request=" + requestId + " surface=" + surfaceId);
        activity.runOnUiThread(() -> {
            Log.i(TAG, "android.platform.create.ui surface=" + surfaceId);
            boolean ok = !destroyed.get() && platform.createSurface(surfaceId);
            Log.i(TAG, "android.platform.create.ui.result=" + ok);
            NativeGateway.completeSurface(nativeHandle, requestId, 0, surfaceId,
                    null, null, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface creation failed");
            Log.i(TAG, "android.platform.create.completed surface=" + surfaceId);
        });
    }

    @SuppressWarnings("unused")
    private void postPresentSurface(
            String requestId, String targetSurfaceId, String sourceSurfaceId, boolean push) {
        Log.i(TAG, "android.platform.present request=" + requestId + " surface=" + targetSurfaceId);
        activity.runOnUiThread(() -> {
            Log.i(TAG, "android.platform.present.ui surface=" + targetSurfaceId);
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
        Log.i(TAG, "android.platform.close request=" + requestId +
                " source=" + sourceSurfaceId + " reveal=" + revealSurfaceId);
        activity.runOnUiThread(() -> {
            boolean ok = !destroyed.get() &&
                    platform.closeAndReveal(sourceSurfaceId, revealSurfaceId);
            Log.i(TAG, "android.platform.close.result request=" + requestId +
                    " source=" + sourceSurfaceId + " reveal=" + revealSurfaceId +
                    " ok=" + ok + " surfaces=" + platform.surfaceCount());
            NativeGateway.completeSurface(nativeHandle, requestId, 3, sourceSurfaceId,
                    sourceSurfaceId, revealSurfaceId, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface close/reveal failed");
        });
    }

    @SuppressWarnings("unused")
    private void postDestroySurface(String requestId, String surfaceId) {
        Log.i(TAG, "android.platform.destroy request=" + requestId +
                " surface=" + surfaceId);
        activity.runOnUiThread(() -> {
            boolean ok = platform.destroySurface(surfaceId);
            Log.i(TAG, "android.platform.destroy.result request=" + requestId +
                    " surface=" + surfaceId + " ok=" + ok +
                    " surfaces=" + platform.surfaceCount() +
                    " nodes=" + platform.nodeCount());
            NativeGateway.completeSurface(nativeHandle, requestId, 4, surfaceId,
                    null, null, -1, ok,
                    ok ? null : "PLATFORM_REJECTED",
                    ok ? null : "Android Surface destroy failed");
        });
    }

    @SuppressWarnings("unused")
    private void postMountTransaction(MountTransaction transaction) {
        Log.i(TAG, "android.platform.mount surface=" + transaction.surfaceId +
                " operations=" + (transaction.operations == null ? -1 : transaction.operations.length));
        activity.runOnUiThread(() -> {
            Log.i(TAG, "android.platform.mount.ui surface=" + transaction.surfaceId);
            boolean ok = !destroyed.get() && platform.apply(transaction);
            Log.i(TAG, "android.platform.mount.result surface=" + transaction.surfaceId +
                    " ok=" + ok);
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
