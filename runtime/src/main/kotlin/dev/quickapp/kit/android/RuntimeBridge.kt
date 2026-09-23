package dev.quickapp.kit.android

import android.os.Handler
import android.os.Looper
import android.widget.FrameLayout
import android.widget.Toast
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

internal class RuntimeBridge(
    private val root: FrameLayout,
    width: Float,
    height: Float,
    rpkPath: String?
) {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val platform: RuntimeSurfaceHost =
        RuntimeSurfaceHost(root, this::dispatchEvent, rpkPath)
    private val destroyed = AtomicBoolean(false)
    private var activeToast: Toast? = null
    private var nativeHandle: Long =
        NativeGateway.create(this, width, height).also {
            if (it == 0L) throw IllegalStateException("native Runtime creation failed")
        }

    fun start(rpkPath: String?) {
        NativeGateway.start(nativeHandle, rpkPath)
    }

    fun setRpkPath(rpkPath: String?) {
        platform.setRpkPath(rpkPath)
    }

    fun dispatchInput(input: QuickAppInput): Boolean {
        return platform.dispatchInput(input.action, input.x, input.y, input.timestampNs)
    }

    fun updateLifecycle(state: QuickAppLifecycleState): Boolean {
        return false
    }

    fun destroy() {
        if (!destroyed.compareAndSet(false, true)) return
        mainHandler.post {
            activeToast?.cancel()
            activeToast = null
        }
        val handle = nativeHandle
        Log.i(
            TAG,
            "android.runtime.destroy.begin surfaces=" + platform.surfaceCount() +
                " nodes=" + platform.nodeCount()
        )
        platform.close()
        if (handle != 0L) NativeGateway.destroy(handle)
        Log.i(
            TAG,
            "android.runtime.destroy.end surfaces=" + platform.surfaceCount() +
                " nodes=" + platform.nodeCount()
        )
    }

    private fun dispatchClick(surfaceId: String, nodeId: String, timestampNs: Long) {
        dispatchEvent(
            surfaceId, nodeId, "click", null, "value", 0.0, false,
            false, false, false, false, 0.0, 0.0, 0.0, false, timestampNs
        )
    }

    private fun dispatchEvent(
        surfaceId: String,
        nodeId: String,
        eventType: String,
        payload: RuntimeSurfaceHost.EventPayload?,
        timestampNs: Long
    ) {
        dispatchEvent(
            surfaceId,
            nodeId,
            eventType,
            payload?.stringValue,
            payload?.numberName ?: "value",
            payload?.numberValue ?: 0.0,
            payload != null && payload.hasNumber,
            payload != null && payload.booleanValue,
            payload != null && payload.hasBoolean,
            payload != null && payload.fromUser,
            payload != null && payload.hasFromUser,
            payload?.scrollOffset ?: 0.0,
            payload?.contentSize ?: 0.0,
            payload?.viewportSize ?: 0.0,
            payload != null && payload.hasScrollMetrics,
            timestampNs
        )
    }

    private fun dispatchEvent(
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
    ) {
        if (!destroyed.get() && nativeHandle != 0L) {
            NativeGateway.dispatchEvent(
                nativeHandle, surfaceId, nodeId, eventType,
                value, numberName, number, hasNumber, checked, hasChecked, fromUser,
                hasFromUser, scrollOffset, contentSize, viewportSize, hasScrollMetrics,
                timestampNs
            )
        }
    }

    private fun postCreateSurface(requestId: String, surfaceId: String) {
        Log.i(TAG, "android.platform.create request=$requestId surface=$surfaceId")
        mainHandler.post {
            Log.i(TAG, "android.platform.create.ui surface=$surfaceId")
            val ok = !destroyed.get() && platform.createSurface(surfaceId)
            Log.i(TAG, "android.platform.create.ui.result=$ok")
            NativeGateway.completeSurface(
                nativeHandle, requestId, 0, surfaceId,
                null, null, -1, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Surface creation failed"
            )
            Log.i(TAG, "android.platform.create.completed surface=$surfaceId")
        }
    }

    private fun postPresentSurface(
        requestId: String,
        targetSurfaceId: String,
        sourceSurfaceId: String?,
        push: Boolean
    ) {
        Log.i(TAG, "android.platform.present request=$requestId surface=$targetSurfaceId")
        mainHandler.post {
            Log.i(TAG, "android.platform.present.ui surface=$targetSurfaceId")
            val ok = !destroyed.get() && (
                if (push) platform.presentPush(sourceSurfaceId, targetSurfaceId)
                else platform.presentRoot(targetSurfaceId)
                )
            NativeGateway.completeSurface(
                nativeHandle, requestId, 1, targetSurfaceId,
                sourceSurfaceId, null, -1, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Surface present failed"
            )
        }
    }

    private fun postSetSurfaceVisibility(
        requestId: String,
        surfaceId: String,
        visible: Boolean
    ) {
        mainHandler.post {
            val ok = !destroyed.get() && platform.setVisible(surfaceId, visible)
            NativeGateway.completeSurface(
                nativeHandle, requestId, 2, surfaceId,
                null, null, if (visible) 1 else 0, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Surface visibility failed"
            )
        }
    }

    private fun postCloseSurface(
        requestId: String,
        sourceSurfaceId: String,
        revealSurfaceId: String
    ) {
        Log.i(
            TAG,
            "android.platform.close request=$requestId" +
                " source=$sourceSurfaceId reveal=$revealSurfaceId"
        )
        mainHandler.post {
            val ok = !destroyed.get() &&
                platform.closeAndReveal(sourceSurfaceId, revealSurfaceId)
            Log.i(
                TAG,
                "android.platform.close.result request=$requestId" +
                    " source=$sourceSurfaceId reveal=$revealSurfaceId" +
                    " ok=$ok surfaces=" + platform.surfaceCount()
            )
            NativeGateway.completeSurface(
                nativeHandle, requestId, 3, sourceSurfaceId,
                sourceSurfaceId, revealSurfaceId, -1, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Surface close/reveal failed"
            )
        }
    }

    private fun postDestroySurface(requestId: String, surfaceId: String) {
        Log.i(TAG, "android.platform.destroy request=$requestId surface=$surfaceId")
        mainHandler.post {
            val ok = platform.destroySurface(surfaceId)
            Log.i(
                TAG,
                "android.platform.destroy.result request=$requestId" +
                    " surface=$surfaceId ok=$ok surfaces=" + platform.surfaceCount() +
                    " nodes=" + platform.nodeCount()
            )
            NativeGateway.completeSurface(
                nativeHandle, requestId, 4, surfaceId,
                null, null, -1, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Surface destroy failed"
            )
        }
    }

    private fun postMountTransaction(transaction: MountTransaction) {
        Log.i(
            TAG,
            "android.platform.mount surface=" + transaction.surfaceId +
                " operations=" + (transaction.operations?.size ?: -1)
        )
        mainHandler.post {
            Log.i(TAG, "android.platform.mount.ui surface=" + transaction.surfaceId)
            val ok = !destroyed.get() && platform.apply(transaction)
            Log.i(
                TAG,
                "android.platform.mount.result surface=" + transaction.surfaceId + " ok=$ok"
            )
            NativeGateway.completeMount(
                nativeHandle, transaction.surfaceId,
                transaction.revision, transaction.mountAttemptId,
                transaction.sourceId, ok,
                if (ok) null else "PLATFORM_REJECTED",
                if (ok) null else "Android Mount transaction failed"
            )
        }
    }

    private fun postShowToast(message: String, durationMs: Long) {
        mainHandler.post {
            if (destroyed.get()) return@post
            activeToast?.cancel()
            val toast = Toast.makeText(root.context, message, Toast.LENGTH_LONG)
            activeToast = toast
            toast.show()
            val resolvedDurationMs = if (durationMs > 0) durationMs else 2000L
            mainHandler.postDelayed({
                if (activeToast !== toast) return@postDelayed
                toast.cancel()
                activeToast = null
            }, resolvedDurationMs)
        }
    }

    private fun onRuntimeStarted(surfaceId: String) {
        Log.i(TAG, "android.runtime.started surface=$surfaceId")
    }

    private fun onRuntimeFailed(errorCode: String, message: String) {
        Log.e(TAG, "android.runtime.failed error=$errorCode message=$message")
    }

    private fun onRuntimeStopped(
        surfaceCount: Int,
        nodeCount: Int,
        handlerCount: Int,
        pendingCallbackCount: Int,
        jsResourceCount: Int,
        coreQueueDepth: Int
    ) {
        mainHandler.post {
            platform.close()
            Log.i(
                TAG,
                "android.runtime.stopped surfaces=$surfaceCount" +
                    " nodes=$nodeCount handlers=$handlerCount" +
                    " pendingCallbacks=$pendingCallbackCount" +
                    " jsResources=$jsResourceCount coreQueue=$coreQueueDepth" +
                    " javaSurfaces=" + platform.surfaceCount() +
                    " javaNodes=" + platform.nodeCount()
            )
            nativeHandle = 0
        }
    }

    companion object {
        private const val TAG = "QuickAppKit"
    }
}
