package dev.quickapp.kit.android

import android.content.Context
import android.os.Looper
import android.view.ViewGroup
import android.widget.FrameLayout
import java.io.File
import kotlin.math.max

class QuickAppRuntime private constructor(context: Context) {
    private enum class State { CREATED, ATTACHED, RUNNING, DESTROYED }

    private val context: Context = context.applicationContext
    private var state = State.CREATED
    private var runtimeRoot: FrameLayout? = null
    private var bridge: RuntimeBridge? = null

    fun attachSurface(container: ViewGroup?): QuickAppResult {
        if (!isMainThread) {
            return QuickAppResult.failed("THREAD_REQUIRED", "attachSurface must run on the main thread")
        }
        if (state == State.DESTROYED) return QuickAppResult.destroyed()
        if (state != State.CREATED || container == null) {
            return QuickAppResult.failed(
                "SURFACE_STATE_INVALID", "Runtime surface is already attached or invalid")
        }
        val root = FrameLayout(container.context)
        root.layoutParams = ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        container.addView(root)
        val density = root.resources.displayMetrics.density
        var widthPx = container.width.toFloat()
        var heightPx = container.height.toFloat()
        if (widthPx <= 1 || heightPx <= 1) {
            widthPx = container.resources.displayMetrics.widthPixels.toFloat()
            heightPx = container.resources.displayMetrics.heightPixels.toFloat()
        }
        val width = max(1f, widthPx) / density
        val height = max(1f, heightPx) / density
        runtimeRoot = root
        bridge = RuntimeBridge(root, width, height, null)
        state = State.ATTACHED
        return QuickAppResult.accepted()
    }

    fun loadRpk(rpk: File?): QuickAppResult {
        if (!isMainThread) {
            return QuickAppResult.failed("THREAD_REQUIRED", "loadRpk must run on the main thread")
        }
        if (state == State.DESTROYED) return QuickAppResult.destroyed()
        val currentBridge = bridge
        if (state != State.ATTACHED || currentBridge == null) {
            return QuickAppResult.failed(
                "RUNTIME_STATE_INVALID", "attachSurface must complete before loadRpk")
        }
        val validation = RpkValidator.validate(rpk)
        if (!validation.isSuccess) return validation
        val path = rpk!!.absolutePath
        currentBridge.setRpkPath(path)
        currentBridge.start(path)
        state = State.RUNNING
        return QuickAppResult.accepted()
    }

    fun dispatchInput(input: QuickAppInput?): QuickAppResult {
        if (!isMainThread) {
            return QuickAppResult.failed("THREAD_REQUIRED", "dispatchInput must run on the main thread")
        }
        if (state == State.DESTROYED) return QuickAppResult.destroyed()
        val currentBridge = bridge
        if (state != State.RUNNING || currentBridge == null || input == null) {
            return QuickAppResult.failed("INPUT_STATE_INVALID", "Runtime is not ready for input")
        }
        return if (currentBridge.dispatchInput(input)) QuickAppResult.accepted()
        else QuickAppResult.failed("INPUT_REJECTED", "Platform rejected input")
    }

    fun updateLifecycle(lifecycle: QuickAppLifecycleState?): QuickAppResult {
        if (!isMainThread) {
            return QuickAppResult.failed("THREAD_REQUIRED", "updateLifecycle must run on the main thread")
        }
        if (state == State.DESTROYED) return QuickAppResult.destroyed()
        val currentBridge = bridge
        if (state != State.RUNNING || lifecycle == null || currentBridge == null) {
            return QuickAppResult.failed("LIFECYCLE_STATE_INVALID", "Runtime is not running")
        }
        return if (currentBridge.updateLifecycle(lifecycle)) QuickAppResult.accepted()
        else QuickAppResult.unsupported("Core lifecycle control is not exposed by this Runtime spine")
    }

    fun destroy(): QuickAppResult {
        if (!isMainThread) {
            return QuickAppResult.failed("THREAD_REQUIRED", "destroy must run on the main thread")
        }
        if (state == State.DESTROYED) return QuickAppResult.completed()
        bridge?.destroy()
        val root = runtimeRoot
        val parent = root?.parent
        if (parent is ViewGroup) {
            parent.removeView(root)
        }
        bridge = null
        runtimeRoot = null
        state = State.DESTROYED
        return QuickAppResult.completed()
    }

    companion object {
        @JvmStatic
        fun create(context: Context?): QuickAppRuntime {
            requireNotNull(context) { "context is required" }
            return QuickAppRuntime(context)
        }

        private val isMainThread: Boolean
            get() = Looper.myLooper() == Looper.getMainLooper()
    }
}
