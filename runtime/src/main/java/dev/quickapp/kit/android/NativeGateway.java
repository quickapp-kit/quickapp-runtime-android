package dev.quickapp.kit.android;

final class NativeGateway {
    static {
        System.loadLibrary("quickapp_android_runtime");
    }

    private NativeGateway() {}

    static native long create(RuntimeBridge bridge, float viewportWidth, float viewportHeight);
    static native void start(long handle, String rpkPath);
    static native void dispatchClick(
            long handle, String surfaceId, String nodeId, long timestampNs);
    static native void dispatchEvent(
            long handle,
            String surfaceId,
            String nodeId,
            String eventType,
            String value,
            String numberName,
            double number,
            boolean hasNumber,
            boolean checked,
            boolean hasChecked,
            boolean fromUser,
            boolean hasFromUser,
            double scrollOffset,
            double contentSize,
            double viewportSize,
            boolean hasScrollMetrics,
            long timestampNs);
    static native void completeSurface(
            long handle,
            String requestId,
            int kind,
            String targetSurfaceId,
            String sourceSurfaceId,
            String revealSurfaceId,
            int visibility,
            boolean completed,
            String errorCode,
            String errorMessage);
    static native void completeMount(
            long handle,
            String surfaceId,
            long revision,
            String mountAttemptId,
            String sourceId,
            boolean mounted,
            String errorCode,
            String errorMessage);
    static native void destroy(long handle);
}
