package dev.quickapp.kit.android

internal object NativeGateway {
    init {
        System.loadLibrary("quickapp_android_runtime")
    }

    external fun create(bridge: RuntimeBridge, viewportWidth: Float, viewportHeight: Float): Long

    external fun start(handle: Long, rpkPath: String?)

    external fun dispatchClick(handle: Long, surfaceId: String, nodeId: String, timestampNs: Long)

    external fun dispatchEvent(
        handle: Long,
        surfaceId: String,
        nodeId: String,
        eventType: String,
        value: String?,
        numberName: String,
        number: Double,
        hasNumber: Boolean,
        checked: Boolean,
        hasChecked: Boolean,
        fromUser: Boolean,
        hasFromUser: Boolean,
        scrollOffset: Double,
        contentSize: Double,
        viewportSize: Double,
        hasScrollMetrics: Boolean,
        timestampNs: Long
    )

    external fun completeSurface(
        handle: Long,
        requestId: String,
        kind: Int,
        targetSurfaceId: String,
        sourceSurfaceId: String?,
        revealSurfaceId: String?,
        visibility: Int,
        completed: Boolean,
        errorCode: String?,
        errorMessage: String?
    )

    external fun completeMount(
        handle: Long,
        surfaceId: String?,
        revision: Long,
        mountAttemptId: String?,
        sourceId: String?,
        mounted: Boolean,
        errorCode: String?,
        errorMessage: String?
    )

    external fun destroy(handle: Long)
}
