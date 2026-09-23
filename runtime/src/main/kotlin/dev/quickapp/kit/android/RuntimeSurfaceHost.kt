package dev.quickapp.kit.android

import android.app.AlertDialog
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.MediaController
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.VideoView
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipFile
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

internal class RuntimeSurfaceHost(
    private val appRoot: FrameLayout,
    private val eventSink: EventSink,
    private var rpkPath: String?
) {
    class EventPayload {
        @JvmField var stringValue: String? = null
        @JvmField var numberName: String = "value"
        @JvmField var numberValue: Double = 0.0
        @JvmField var hasNumber: Boolean = false
        @JvmField var booleanValue: Boolean = false
        @JvmField var hasBoolean: Boolean = false
        @JvmField var fromUser: Boolean = false
        @JvmField var hasFromUser: Boolean = false
        @JvmField var scrollOffset: Double = 0.0
        @JvmField var contentSize: Double = 0.0
        @JvmField var viewportSize: Double = 0.0
        @JvmField var hasScrollMetrics: Boolean = false
    }

    fun interface EventSink {
        fun onEvent(
            surfaceId: String,
            nodeId: String,
            eventType: String,
            payload: EventPayload?,
            timestampNs: Long
        )
    }

    private class NodeRecord(@JvmField val surfaceId: String, @JvmField val view: View) {
        @JvmField var backgroundColor = Color.TRANSPARENT
        @JvmField var borderRadius = 0f
        @JvmField var suppressEvents = false
        @JvmField var sliderMin = 0.0
        @JvmField var sliderMax = 0.0
        @JvmField var sliderStep = 1.0
        @JvmField var pickerOptions = arrayOf<String>()
        @JvmField var pickerSelected = 0
        @JvmField var atTop = false
        @JvmField var atBottom = false
        @JvmField var scrollEndRunnable: Runnable? = null
        @JvmField var videoView: VideoView? = null
        @JvmField var videoPoster: ImageView? = null
        @JvmField var videoPlayer: android.media.MediaPlayer? = null
        @JvmField var videoProgressRunnable: Runnable? = null
        @JvmField var videoTimeLabel: TextView? = null
        @JvmField var videoProgressBar: SeekBar? = null
        @JvmField var videoPlayButton: ImageButton? = null
        @JvmField var videoController: MediaController? = null
        @JvmField var videoAutoplay = false
        @JvmField var videoControls = false
        @JvmField var videoMuted = false
        @JvmField var videoPlayRequested = false
        @JvmField var tabsView: LinearLayout? = null
        @JvmField var tabsItems = arrayOf<String>()
        @JvmField var tabsSelected = 0
        @JvmField var tabsTextColor = Color.WHITE
    }

    private val density: Float = appRoot.resources.displayMetrics.density
    private val surfaces = HashMap<String, FrameLayout>()
    private val nodes = HashMap<String, NodeRecord>()
    private val materializedVideos = HashMap<String, File>()

    init {
        appRoot.isFocusableInTouchMode = true
    }

    fun setRpkPath(rpkPath: String?) {
        this.rpkPath = rpkPath
    }

    fun dispatchInput(action: Int, x: Float, y: Float, timestampNs: Long): Boolean {
        val eventTimeMs =
            if (timestampNs > 0) timestampNs / 1_000_000L else System.currentTimeMillis()
        val event = MotionEvent.obtain(eventTimeMs, eventTimeMs, action, x, y, 0)
        return try {
            appRoot.dispatchTouchEvent(event)
        } finally {
            event.recycle()
        }
    }

    fun createSurface(surfaceId: String?): Boolean {
        if (surfaceId == null || surfaces.containsKey(surfaceId)) {
            return false
        }
        val container = FrameLayout(appRoot.context)
        container.visibility = View.INVISIBLE
        container.setBackgroundColor(Color.WHITE)
        appRoot.addView(
            container,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        surfaces[surfaceId] = container
        return true
    }

    fun presentRoot(targetSurfaceId: String?): Boolean {
        val target = surfaces[targetSurfaceId] ?: return false
        target.visibility = View.VISIBLE
        return true
    }

    fun presentPush(sourceSurfaceId: String?, targetSurfaceId: String?): Boolean {
        val source = surfaces[sourceSurfaceId]
        val target = surfaces[targetSurfaceId]
        if (source == null || target == null || source.visibility != View.VISIBLE) {
            return false
        }
        source.visibility = View.INVISIBLE
        target.visibility = View.VISIBLE
        return true
    }

    fun setVisible(surfaceId: String?, visible: Boolean): Boolean {
        val surface = surfaces[surfaceId] ?: return false
        surface.visibility = if (visible) View.VISIBLE else View.INVISIBLE
        return true
    }

    fun closeAndReveal(sourceSurfaceId: String?, revealSurfaceId: String?): Boolean {
        val source = surfaces[sourceSurfaceId]
        val reveal = surfaces[revealSurfaceId]
        if (source == null || reveal == null) {
            return false
        }
        // Core sends a separate DestroySurface after close/onDestroy. Keep the
        // platform surface registered until that command arrives.
        source.visibility = View.INVISIBLE
        reveal.visibility = View.VISIBLE
        return true
    }

    fun destroySurface(surfaceId: String?): Boolean {
        if (surfaceId == null || !surfaces.containsKey(surfaceId)) {
            return false
        }
        removeSurface(surfaceId)
        return true
    }

    fun apply(transaction: MountTransaction): Boolean {
        val surface = surfaces[transaction.surfaceId]
        val operations = transaction.operations
        if (surface == null || operations == null) {
            return false
        }
        val surfaceId = transaction.surfaceId ?: return false
        if (transaction.full) {
            clearSurface(surfaceId)
        }
        return try {
            for (operation in operations) {
                if (!applyOperation(surfaceId, surface, operation)) {
                    Log.e(
                        TAG,
                        "android.mount.operation.failed surface=" + surfaceId +
                            " kind=" + (operation?.kind ?: "null") +
                            " node=" + (operation?.nodeId ?: "") +
                            " property=" + (operation?.propertyName ?: "")
                    )
                    return false
                }
            }
            true
        } catch (failure: RuntimeException) {
            Log.e(TAG, "android.mount.exception surface=" + surfaceId, failure)
            false
        }
    }

    fun surfaceCount(): Int = surfaces.size

    fun nodeCount(): Int = nodes.size

    fun close() {
        for (surface in surfaces.values) {
            appRoot.removeView(surface)
        }
        nodes.clear()
        surfaces.clear()
        for (video in materializedVideos.values) {
            video.delete()
        }
        materializedVideos.clear()
    }

    private fun applyOperation(
        surfaceId: String,
        surface: FrameLayout,
        operation: MountOperation?
    ): Boolean {
        if (operation == null || operation.nodeId == null) {
            return false
        }
        return when (operation.kind) {
            MountOperation.CREATE -> createNode(surfaceId, surface, operation)
            MountOperation.SET_PROP -> setProperty(surfaceId, operation)
            MountOperation.SET_LAYOUT -> setLayout(surfaceId, operation)
            MountOperation.INSERT, MountOperation.MOVE -> moveNode(surfaceId, operation)
            MountOperation.REMOVE -> removeNode(surfaceId, operation.nodeId)
            else -> false
        }
    }

    private fun createNode(
        surfaceId: String,
        surface: FrameLayout,
        operation: MountOperation
    ): Boolean {
        val nodeId = operation.nodeId ?: return false
        val key = key(surfaceId, nodeId)
        if (nodes.containsKey(key)) {
            return false
        }
        val view: View
        when (operation.componentType) {
            COMPONENT_TEXT -> {
                val text = TextView(appRoot.context)
                text.gravity = Gravity.START or Gravity.CENTER_VERTICAL
                view = text
            }
            COMPONENT_BUTTON -> {
                val button = Button(appRoot.context)
                button.isAllCaps = false
                button.gravity = Gravity.CENTER
                button.setPadding(0, 0, 0, 0)
                button.setOnTouchListener { _, event ->
                    if (event.action == MotionEvent.ACTION_DOWN) {
                        appRoot.requestFocus()
                    }
                    false
                }
                button.setOnClickListener {
                    Log.i(TAG, "android.input.click surface=$surfaceId node=$nodeId")
                    eventSink.onEvent(
                        surfaceId, nodeId, "click", EventPayload(),
                        android.os.SystemClock.elapsedRealtimeNanos()
                    )
                }
                view = button
            }
            COMPONENT_VIEW -> view = FrameLayout(appRoot.context)
            COMPONENT_IMAGE -> {
                val image = ImageView(appRoot.context)
                image.scaleType = ImageView.ScaleType.FIT_CENTER
                view = image
            }
            COMPONENT_INPUT -> {
                val input = EditText(appRoot.context)
                input.isSingleLine = true
                input.addTextChangedListener(object : TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                        if (!isSuppressed(surfaceId, nodeId)) {
                            val payload = EventPayload()
                            payload.stringValue = s?.toString() ?: ""
                            eventSink.onEvent(
                                surfaceId, nodeId, "input", payload,
                                android.os.SystemClock.elapsedRealtimeNanos()
                            )
                        }
                    }
                    override fun afterTextChanged(s: Editable?) {}
                })
                input.setOnFocusChangeListener { _, focused ->
                    if (!isSuppressed(surfaceId, nodeId)) {
                        val payload = EventPayload()
                        payload.stringValue = input.text.toString()
                        eventSink.onEvent(
                            surfaceId, nodeId, if (focused) "focus" else "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos()
                        )
                    }
                }
                view = input
            }
            COMPONENT_SWITCH -> {
                val toggle = Switch(appRoot.context)
                toggle.setOnTouchListener { _, event ->
                    if (event.action == MotionEvent.ACTION_DOWN) {
                        appRoot.requestFocus()
                    }
                    false
                }
                toggle.setOnCheckedChangeListener { _, checked ->
                    if (!isSuppressed(surfaceId, nodeId)) {
                        val payload = EventPayload()
                        payload.booleanValue = checked
                        payload.hasBoolean = true
                        eventSink.onEvent(
                            surfaceId, nodeId, "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos()
                        )
                    }
                }
                view = toggle
            }
            COMPONENT_SLIDER -> {
                val slider = SeekBar(appRoot.context)
                slider.setOnTouchListener { _, event ->
                    if (event.action == MotionEvent.ACTION_DOWN) {
                        appRoot.requestFocus()
                    }
                    false
                }
                slider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                        if (!fromUser || isSuppressed(surfaceId, nodeId)) return
                        val node = nodes[key(surfaceId, nodeId)] ?: return
                        val payload = EventPayload()
                        payload.numberValue = sliderValue(node, progress)
                        payload.hasNumber = true
                        payload.fromUser = true
                        payload.hasFromUser = true
                        Log.i(
                            TAG,
                            "android.input.slider.change surface=$surfaceId node=$nodeId" +
                                " value=" + payload.numberValue + " isFromUser=true"
                        )
                        eventSink.onEvent(
                            surfaceId, nodeId, "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos()
                        )
                    }
                    override fun onStartTrackingTouch(bar: SeekBar?) {}
                    override fun onStopTrackingTouch(bar: SeekBar?) {}
                })
                view = slider
            }
            COMPONENT_PICKER -> {
                val picker = TextView(appRoot.context)
                picker.gravity = Gravity.START or Gravity.CENTER_VERTICAL
                picker.isClickable = true
                picker.setOnTouchListener { _, event ->
                    if (event.action == MotionEvent.ACTION_DOWN) {
                        appRoot.requestFocus()
                    }
                    false
                }
                picker.setOnClickListener { showPicker(surfaceId, nodeId, picker) }
                view = picker
            }
            COMPONENT_LIST -> {
                val list = FrameLayout(appRoot.context)
                list.clipChildren = false
                view = list
            }
            COMPONENT_SCROLL -> {
                val scroll = ScrollView(appRoot.context)
                scroll.isFillViewport = false
                scroll.clipToPadding = true
                scroll.setOnScrollChangeListener { _, _, scrollY, _, oldScrollY ->
                    val node = nodes[key(surfaceId, nodeId)]
                    if (node == null || node.suppressEvents || scrollY == oldScrollY) {
                        return@setOnScrollChangeListener
                    }
                    emitScroll(surfaceId, nodeId, scroll, node, "scroll")
                    val atTop = scrollY <= 0
                    val atBottom = scrollY >= scrollRange(scroll)
                    if (atTop && !node.atTop) {
                        emitScroll(surfaceId, nodeId, scroll, node, "scrolltop")
                    }
                    if (atBottom && !node.atBottom) {
                        emitScroll(surfaceId, nodeId, scroll, node, "scrollbottom")
                    }
                    node.atTop = atTop
                    node.atBottom = atBottom
                    node.scrollEndRunnable?.let { scroll.removeCallbacks(it) }
                    val endRunnable = Runnable {
                        if (!node.suppressEvents && nodes.containsKey(key(surfaceId, nodeId))) {
                            emitScroll(surfaceId, nodeId, scroll, node, "scrollend")
                        }
                    }
                    node.scrollEndRunnable = endRunnable
                    scroll.postDelayed(endRunnable, 120L)
                }
                view = scroll
            }
            COMPONENT_TABS -> {
                val tabs = LinearLayout(appRoot.context)
                tabs.orientation = LinearLayout.HORIZONTAL
                tabs.gravity = Gravity.CENTER_VERTICAL
                tabs.isBaselineAligned = false
                tabs.clipToOutline = true
                view = tabs
            }
            COMPONENT_VIDEO -> {
                view = createVideoNode(surfaceId, nodeId) ?: return false
            }
            else -> return false
        }
        view.tag = nodeId
        surface.addView(view, FrameLayout.LayoutParams(0, 0))
        val record = NodeRecord(surfaceId, view)
        if (operation.componentType == COMPONENT_SCROLL) {
            record.atTop = true
            record.atBottom = false
        }
        if (operation.componentType == COMPONENT_VIDEO) {
            val container = view as ViewGroup
            for (index in 0 until container.childCount) {
                val child = container.getChildAt(index)
                if (child is ImageView) {
                    record.videoPoster = child
                } else if (child is VideoView) {
                    record.videoView = child
                }
            }
            if (record.videoPoster == null || record.videoView == null) {
                return false
            }
        }
        if (operation.componentType == COMPONENT_TABS) {
            record.tabsView = view as LinearLayout
        }
        nodes[key] = record
        return true
    }

    private fun createVideoNode(surfaceId: String, nodeId: String): View? {
        val container = FrameLayout(appRoot.context)
        container.setBackgroundColor(Color.rgb(32, 37, 43))
        val poster = ImageView(appRoot.context)
        poster.scaleType = ImageView.ScaleType.FIT_CENTER
        poster.setBackgroundColor(Color.rgb(32, 37, 43))
        container.addView(
            poster,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        val video = VideoView(appRoot.context)
        video.setBackgroundColor(Color.TRANSPARENT)
        video.alpha = 0f
        container.addView(
            video,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        poster.bringToFront()
        video.setOnPreparedListener { player ->
            val node = nodes[key(surfaceId, nodeId)] ?: return@setOnPreparedListener
            node.videoPlayer = player
            if (node.videoMuted) player.setVolume(0f, 0f)
            node.videoView?.visibility = View.VISIBLE
            node.videoView?.alpha = 1f
            node.videoPoster?.visibility = View.INVISIBLE
            node.videoView?.bringToFront()
            Log.i(TAG, "android.video.prepared surface=$surfaceId node=$nodeId")
            emitVideoEvent(surfaceId, nodeId, "prepared", null)
            scheduleVideoTimeUpdates(surfaceId, nodeId, video, node)
            if (node.videoAutoplay || node.videoPlayRequested) {
                node.videoPlayRequested = false
                video.start()
                Log.i(
                    TAG,
                    "android.video.start surface=$surfaceId node=$nodeId" +
                        " autoplay=" + node.videoAutoplay
                )
                emitVideoEvent(surfaceId, nodeId, "start", null)
            }
        }
        video.setOnCompletionListener {
            stopVideoTimeUpdates(surfaceId, nodeId, video)
            Log.i(TAG, "android.video.finish surface=$surfaceId node=$nodeId")
            emitVideoEvent(surfaceId, nodeId, "finish", null)
        }
        video.setOnErrorListener { _, what, extra ->
            stopVideoTimeUpdates(surfaceId, nodeId, video)
            video.visibility = View.INVISIBLE
            video.alpha = 0f
            val node = nodes[key(surfaceId, nodeId)]
            if (node?.videoPoster != null) {
                node.videoPoster?.visibility = View.VISIBLE
                node.videoPoster?.bringToFront()
            }
            Log.i(
                TAG,
                "android.video.error surface=$surfaceId node=$nodeId what=$what extra=$extra"
            )
            emitVideoEvent(surfaceId, nodeId, "error", null)
            true
        }
        video.setOnClickListener {
            val node = nodes[key(surfaceId, nodeId)] ?: return@setOnClickListener
            if (node.videoPlayer == null) {
                node.videoPlayRequested = true
                Log.i(TAG, "android.video.play.pending surface=$surfaceId node=$nodeId")
                node.videoController?.show()
                return@setOnClickListener
            }
            if (video.isPlaying) {
                video.pause()
                emitVideoEvent(surfaceId, nodeId, "pause", null)
            } else {
                video.start()
                emitVideoEvent(surfaceId, nodeId, "start", null)
            }
            node.videoController?.show()
        }
        poster.setOnClickListener { video.performClick() }
        return container
    }

    private fun setProperty(surfaceId: String, operation: MountOperation): Boolean {
        val node = nodes[key(surfaceId, operation.nodeId)]
        if (node == null || operation.propertyName == null) {
            return false
        }
        val nodeId = operation.nodeId ?: return false
        val view = node.view
        if (node.videoView != null) {
            Log.i(
                TAG,
                "android.video.property node=$nodeId name=" + operation.propertyName +
                    " valueKind=" + operation.valueKind
            )
        }
        when (operation.propertyName) {
            "text" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING) return false
                if (view is TextView) {
                    view.text = operation.stringValue
                    return true
                }
                return false
            }
            "items" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                    node.tabsView == null || operation.stringValue == null
                ) {
                    return false
                }
                node.tabsItems = operation.stringValue.split("|").toTypedArray()
                if (node.tabsItems.isEmpty()) return false
                node.tabsSelected = clampTabsIndex(node.tabsSelected, node.tabsItems.size)
                rebuildTabs(surfaceId, nodeId, node)
                return true
            }
            "src" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING) return false
                if (view is ImageView) {
                    return loadImage(view, operation.stringValue)
                }
                val videoView = node.videoView
                if (videoView != null) {
                    return try {
                        val source = videoUri(operation.stringValue) ?: return false
                        Log.i(TAG, "android.video.set_uri node=$nodeId uri=$source")
                        videoView.setVideoURI(source)
                        true
                    } catch (failure: RuntimeException) {
                        Log.w(TAG, "android.video.source.failed node=$nodeId", failure)
                        false
                    }
                }
                return false
            }
            "poster" -> {
                val poster = node.videoPoster
                if (operation.valueKind != MountOperation.VALUE_STRING || poster == null) {
                    return false
                }
                return operation.stringValue == null || operation.stringValue.isEmpty() ||
                    loadImage(poster, operation.stringValue)
            }
            "autoplay" -> {
                val videoView = node.videoView
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN || videoView == null) {
                    return false
                }
                node.videoAutoplay = operation.booleanValue
                if (node.videoAutoplay && node.videoPlayer != null && !videoView.isPlaying) {
                    videoView.start()
                    emitVideoEvent(surfaceId, nodeId, "start", null)
                }
                return true
            }
            "controls" -> {
                val videoView = node.videoView
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN || videoView == null) {
                    return false
                }
                node.videoControls = operation.booleanValue
                videoView.setMediaController(null)
                node.videoController = null
                if (!node.videoControls) return true
                val controls = MediaController(appRoot.context)
                controls.setAnchorView(videoView)
                node.videoController = controls
                videoView.setMediaController(controls)
                return true
            }
            "muted" -> {
                val videoView = node.videoView
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN || videoView == null) {
                    return false
                }
                node.videoMuted = operation.booleanValue
                node.videoPlayer?.let {
                    val volume = if (node.videoMuted) 0f else 1f
                    it.setVolume(volume, volume)
                }
                return true
            }
            "value" -> {
                if (view is EditText) {
                    if (operation.valueKind != MountOperation.VALUE_STRING) return false
                    suppress(surfaceId, nodeId, true)
                    try {
                        view.setText(operation.stringValue)
                    } finally {
                        suppress(surfaceId, nodeId, false)
                    }
                    return true
                }
                if (view is SeekBar) {
                    if (operation.valueKind != MountOperation.VALUE_NUMBER) return false
                    updateSlider(
                        view, node, node.sliderMin, node.sliderMax, node.sliderStep,
                        operation.numberValue
                    )
                    return true
                }
                return false
            }
            "checked" -> {
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN || view !is Switch) {
                    return false
                }
                suppress(surfaceId, nodeId, true)
                try {
                    view.isChecked = operation.booleanValue
                } finally {
                    suppress(surfaceId, nodeId, false)
                }
                return true
            }
            "min" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER || view !is SeekBar) {
                    return false
                }
                node.sliderMin = operation.numberValue
                updateSlider(view, node, node.sliderMin, node.sliderMax, node.sliderStep, null)
                return true
            }
            "max" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER || view !is SeekBar) {
                    return false
                }
                node.sliderMax = operation.numberValue
                updateSlider(view, node, node.sliderMin, node.sliderMax, node.sliderStep, null)
                return true
            }
            "step" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER || view !is SeekBar ||
                    operation.numberValue <= 0
                ) {
                    return false
                }
                node.sliderStep = operation.numberValue
                updateSlider(view, node, node.sliderMin, node.sliderMax, node.sliderStep, null)
                return true
            }
            "range" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING || view !is TextView) {
                    return false
                }
                node.pickerOptions = if (operation.stringValue == null) {
                    arrayOf()
                } else {
                    operation.stringValue.split("|").toTypedArray()
                }
                updatePickerText(view, node)
                return true
            }
            "selected" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER) return false
                if (node.tabsView != null) {
                    node.tabsSelected = clampTabsIndex(
                        operation.numberValue.roundToInt(), node.tabsItems.size
                    )
                    rebuildTabs(surfaceId, nodeId, node)
                    return true
                }
                if (view !is TextView) return false
                node.pickerSelected = clampPickerIndex(
                    operation.numberValue.roundToInt(), node.pickerOptions.size
                )
                updatePickerText(view, node)
                return true
            }
            "mode" -> {
                return operation.valueKind == MountOperation.VALUE_STRING &&
                    "text" == operation.stringValue && view is TextView
            }
            "enabled" -> {
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN) return false
                view.isEnabled = operation.booleanValue
                return true
            }
            "backgroundColor" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING) return false
                node.backgroundColor = Color.parseColor(operation.stringValue)
                applyBackground(node)
                if (node.tabsView != null) applyTabsSelection(node)
                return true
            }
            "color" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING) return false
                if (node.tabsView != null) {
                    node.tabsTextColor = Color.parseColor(operation.stringValue)
                    applyTabsSelection(node)
                    return true
                }
                if (view !is TextView) return false
                view.setTextColor(Color.parseColor(operation.stringValue))
                return true
            }
            "borderRadius" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER) return false
                node.borderRadius = logical(operation.numberValue).toFloat()
                applyBackground(node)
                if (node.tabsView != null) applyTabsSelection(node)
                return true
            }
            "fontSize" -> {
                if (operation.valueKind != MountOperation.VALUE_NUMBER || view !is TextView) {
                    return false
                }
                view.textSize = operation.numberValue.toFloat()
                return true
            }
            "textAlign" -> {
                if (operation.valueKind != MountOperation.VALUE_STRING || view !is TextView) {
                    return false
                }
                val horizontal = when (operation.stringValue) {
                    "center" -> Gravity.CENTER_HORIZONTAL
                    "right" -> Gravity.END
                    else -> Gravity.START
                }
                view.gravity = horizontal or Gravity.CENTER_VERTICAL
                return true
            }
            else -> return false
        }
    }

    private fun setLayout(surfaceId: String, operation: MountOperation): Boolean {
        val node = nodes[key(surfaceId, operation.nodeId)]
        if (node == null || operation.width < 0 || operation.height < 0) {
            return false
        }
        val params = FrameLayout.LayoutParams(
            logical(operation.width.toDouble()), logical(operation.height.toDouble())
        )
        params.leftMargin = logical(operation.x.toDouble())
        params.topMargin = logical(operation.y.toDouble())
        node.view.layoutParams = params
        if (node.tabsView != null) {
            Log.i(
                TAG,
                "android.tabs.layout surface=$surfaceId node=" + operation.nodeId +
                    " x=" + operation.x + " y=" + operation.y +
                    " width=" + operation.width + " height=" + operation.height
            )
        }
        return true
    }

    private fun moveNode(surfaceId: String, operation: MountOperation): Boolean {
        val child = nodes[key(surfaceId, operation.nodeId)]
        val parent = nodes[key(surfaceId, operation.parentNodeId)]
        if (child == null || parent == null || parent.view !is ViewGroup) {
            return false
        }
        val parentGroup = parent.view
        if (isDescendant(parentGroup, child.view)) {
            return false
        }
        val current = child.view.parent as? ViewGroup
        current?.removeView(child.view)
        val index = max(0, min(operation.index, parentGroup.childCount))
        parentGroup.addView(child.view, index)
        if (child.tabsView != null) {
            Log.i(
                TAG,
                "android.tabs.mount surface=$surfaceId node=" + operation.nodeId +
                    " parent=" + operation.parentNodeId + " index=" + index +
                    " parentClass=" + parent.view.javaClass.simpleName
            )
        }
        return true
    }

    private fun removeNode(surfaceId: String, nodeId: String?): Boolean {
        val node = nodes[key(surfaceId, nodeId)] ?: return false
        removeNodeRecursive(surfaceId, node.view)
        return true
    }

    private fun removeNodeRecursive(surfaceId: String, view: View) {
        if (view is ViewGroup) {
            while (view.childCount > 0) {
                removeNodeRecursive(surfaceId, view.getChildAt(0))
            }
        }
        val tag = view.tag
        val record = if (tag is String) nodes[key(surfaceId, tag)] else null
        if (tag is String) {
            nodes.remove(key(surfaceId, tag))
        }
        (view.parent as? ViewGroup)?.removeView(view)
        view.setOnClickListener(null)
        view.onFocusChangeListener = null
        if (view is Switch) view.setOnCheckedChangeListener(null)
        if (view is ScrollView) {
            if (record?.scrollEndRunnable != null) {
                view.removeCallbacks(record.scrollEndRunnable)
            }
            view.setOnScrollChangeListener(null as View.OnScrollChangeListener?)
        }
        val videoView = record?.videoView
        if (videoView != null) {
            stopVideoTimeUpdates(surfaceId, view.tag as? String ?: "", videoView)
            videoView.setOnPreparedListener(null)
            videoView.setOnCompletionListener(null)
            videoView.setOnErrorListener(null)
            videoView.setOnClickListener(null)
            videoView.setMediaController(null)
            record.videoController = null
            videoView.stopPlayback()
            record.videoPlayer = null
        }
    }

    private fun clearSurface(surfaceId: String) {
        val surface = surfaces[surfaceId] ?: return
        while (surface.childCount > 0) {
            removeNodeRecursive(surfaceId, surface.getChildAt(0))
        }
        nodes.entries.removeIf { it.value.surfaceId == surfaceId }
    }

    private fun removeSurface(surfaceId: String) {
        val surface = surfaces.remove(surfaceId) ?: return
        clearSurfaceContents(surfaceId, surface)
        appRoot.removeView(surface)
    }

    private fun clearSurfaceContents(surfaceId: String, surface: FrameLayout) {
        while (surface.childCount > 0) {
            removeNodeRecursive(surfaceId, surface.getChildAt(0))
        }
        nodes.entries.removeIf { it.value.surfaceId == surfaceId }
    }

    private fun applyBackground(node: NodeRecord) {
        val drawable = GradientDrawable()
        drawable.setColor(node.backgroundColor)
        drawable.cornerRadius = if (node.tabsView == null) {
            node.borderRadius
        } else {
            max(node.borderRadius, logical(20.0).toFloat())
        }
        node.view.background = drawable
    }

    private fun updateSlider(
        slider: SeekBar,
        node: NodeRecord,
        min: Double,
        max: Double,
        step: Double,
        value: Double?
    ) {
        if (!min.isFinite() || !max.isFinite() || !step.isFinite() || step <= 0 || max < min) {
            return
        }
        val count = max(1, ((max - min) / step).roundToInt())
        slider.max = count
        if (value != null) {
            suppress(node.surfaceId, node.view.tag as? String, true)
            try {
                slider.progress = clampSliderProgress(value, min, step, count)
            } finally {
                suppress(node.surfaceId, node.view.tag as? String, false)
            }
        }
    }

    private fun clampSliderProgress(value: Double, min: Double, step: Double, count: Int): Int {
        val progress = ((value - min) / step).roundToInt()
        return max(0, min(count, progress))
    }

    private fun sliderValue(node: NodeRecord, progress: Int): Double {
        val value = node.sliderMin + progress * node.sliderStep
        return min(node.sliderMax, max(node.sliderMin, value))
    }

    private fun clampPickerIndex(index: Int, size: Int): Int {
        if (size <= 0) return 0
        return max(0, min(size - 1, index))
    }

    private fun clampTabsIndex(index: Int, size: Int): Int {
        if (size <= 0) return 0
        return max(0, min(size - 1, index))
    }

    private fun rebuildTabs(surfaceId: String, nodeId: String, node: NodeRecord) {
        val tabsView = node.tabsView ?: return
        tabsView.removeAllViews()
        for (index in node.tabsItems.indices) {
            val tab = TextView(appRoot.context)
            tab.gravity = Gravity.CENTER
            tab.text = node.tabsItems[index]
            tab.textSize = 14f
            tab.setTextColor(node.tabsTextColor)
            tab.setOnClickListener {
                val current = nodes[key(surfaceId, nodeId)]
                if (current == null || current.tabsItems.isEmpty()) return@setOnClickListener
                current.tabsSelected = clampTabsIndex(index, current.tabsItems.size)
                applyTabsSelection(current)
                val payload = EventPayload()
                payload.numberName = "index"
                payload.numberValue = current.tabsSelected.toDouble()
                payload.hasNumber = true
                payload.stringValue = current.tabsItems[current.tabsSelected]
                Log.i(
                    TAG,
                    "android.input.tabs.change surface=$surfaceId node=$nodeId" +
                        " index=" + current.tabsSelected + " value=" + payload.stringValue
                )
                eventSink.onEvent(
                    surfaceId, nodeId, "change", payload,
                    android.os.SystemClock.elapsedRealtimeNanos()
                )
            }
            tabsView.addView(
                tab,
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f)
            )
        }
        applyTabsSelection(node)
    }

    private fun applyTabsSelection(node: NodeRecord) {
        val tabsView = node.tabsView ?: return
        node.tabsSelected = clampTabsIndex(node.tabsSelected, node.tabsItems.size)
        for (index in 0 until tabsView.childCount) {
            val child = tabsView.getChildAt(index)
            if (child !is TextView) continue
            child.setTextColor(
                if (index == node.tabsSelected) node.backgroundColor else node.tabsTextColor
            )
            child.alpha = if (index == node.tabsSelected) 1f else 0.65f
            child.setTypeface(
                null,
                if (index == node.tabsSelected) android.graphics.Typeface.BOLD
                else android.graphics.Typeface.NORMAL
            )
            if (index == node.tabsSelected) {
                val selectedBackground = GradientDrawable()
                selectedBackground.setColor(Color.WHITE)
                selectedBackground.cornerRadius = max(node.borderRadius, logical(20.0).toFloat())
                child.background = selectedBackground
            } else {
                child.background = null
            }
        }
    }

    private fun updatePickerText(picker: TextView, node: NodeRecord) {
        if (node.pickerOptions.isEmpty()) {
            picker.text = ""
        } else {
            node.pickerSelected = clampPickerIndex(node.pickerSelected, node.pickerOptions.size)
            picker.text = node.pickerOptions[node.pickerSelected]
        }
    }

    private fun scrollRange(scroll: ScrollView): Int {
        if (scroll.childCount == 0) return 0
        val child = scroll.getChildAt(0)
        return max(0, child.height - scroll.height)
    }

    private fun emitScroll(
        surfaceId: String,
        nodeId: String,
        scroll: ScrollView,
        node: NodeRecord,
        eventType: String
    ) {
        val payload = EventPayload()
        payload.scrollOffset = scroll.scrollY / density.toDouble()
        payload.contentSize = if (scroll.childCount == 0) {
            0.0
        } else {
            scroll.getChildAt(0).height / density.toDouble()
        }
        payload.viewportSize = scroll.height / density.toDouble()
        payload.hasScrollMetrics = true
        Log.i(
            TAG,
            "android.input.$eventType surface=$surfaceId node=$nodeId" +
                " scrollOffset=" + payload.scrollOffset +
                " contentSize=" + payload.contentSize +
                " viewportSize=" + payload.viewportSize
        )
        eventSink.onEvent(
            surfaceId, nodeId, eventType, payload,
            android.os.SystemClock.elapsedRealtimeNanos()
        )
    }

    private fun emitVideoEvent(
        surfaceId: String,
        nodeId: String,
        eventType: String,
        currentTime: Double?
    ) {
        val payload = EventPayload()
        if (currentTime != null) {
            payload.numberName = "currentTime"
            payload.numberValue = currentTime
            payload.hasNumber = true
        }
        eventSink.onEvent(
            surfaceId, nodeId, eventType, payload,
            android.os.SystemClock.elapsedRealtimeNanos()
        )
    }

    private fun scheduleVideoTimeUpdates(
        surfaceId: String,
        nodeId: String,
        video: VideoView,
        node: NodeRecord
    ) {
        if (node.videoProgressRunnable != null) return
        val runnable = object : Runnable {
            override fun run() {
                if (!nodes.containsKey(key(surfaceId, nodeId)) || node.videoView != video) return
                if (video.isPlaying) {
                    emitVideoEvent(surfaceId, nodeId, "timeupdate", video.currentPosition / 1000.0)
                    node.videoProgressRunnable?.let { video.postDelayed(it, 500L) }
                }
            }
        }
        node.videoProgressRunnable = runnable
        video.postDelayed(runnable, 500L)
    }

    private fun stopVideoTimeUpdates(surfaceId: String, nodeId: String, video: VideoView) {
        val node = nodes[key(surfaceId, nodeId)]
        if (node?.videoProgressRunnable != null) {
            video.removeCallbacks(node.videoProgressRunnable)
            node.videoProgressRunnable = null
        }
    }

    private fun showPicker(surfaceId: String, nodeId: String, picker: TextView) {
        val node = nodes[key(surfaceId, nodeId)] ?: return
        if (node.pickerOptions.isEmpty()) return
        val pending = intArrayOf(node.pickerSelected)
        AlertDialog.Builder(appRoot.context)
            .setTitle("选择")
            .setSingleChoiceItems(node.pickerOptions, node.pickerSelected) { _, which ->
                pending[0] = which
            }
            .setNegativeButton("取消", null)
            .setPositiveButton("确认") { _, _ ->
                node.pickerSelected = clampPickerIndex(pending[0], node.pickerOptions.size)
                updatePickerText(picker, node)
                val payload = EventPayload()
                payload.numberValue = node.pickerSelected.toDouble()
                payload.numberName = "selected"
                payload.hasNumber = true
                payload.stringValue = node.pickerOptions[node.pickerSelected]
                Log.i(
                    TAG,
                    "android.input.picker.change surface=$surfaceId node=$nodeId" +
                        " selected=" + node.pickerSelected + " value=" + payload.stringValue
                )
                eventSink.onEvent(
                    surfaceId, nodeId, "change", payload,
                    android.os.SystemClock.elapsedRealtimeNanos()
                )
            }
            .show()
    }

    private fun loadImage(image: ImageView, member: String?): Boolean {
        val path = rpkPath
        if (member.isNullOrEmpty() || path == null) return false
        return try {
            ZipFile(path).use { packageFile ->
                val entry = packageFile.getEntry(member) ?: return false
                packageFile.getInputStream(entry).use { input ->
                    val bitmap = android.graphics.BitmapFactory.decodeStream(input)
                        ?: return false
                    image.setImageBitmap(bitmap)
                    true
                }
            }
        } catch (failure: IOException) {
            Log.w(TAG, "android.image.load.failed member=$member", failure)
            false
        }
    }

    private fun videoUri(source: String?): Uri? {
        if (source.isNullOrEmpty()) return null
        if (!source.startsWith("assets/")) return Uri.parse(source)
        val cached = materializedVideos[source]
        if (cached != null && cached.isFile) return Uri.fromFile(cached)
        val path = rpkPath ?: return null
        val safeName = source.replace(Regex("[^A-Za-z0-9._-]"), "_")
        val output = File(appRoot.context.cacheDir, "quickapp-kit-video-$safeName")
        return try {
            ZipFile(path).use { packageFile ->
                val entry = packageFile.getEntry(source) ?: return null
                packageFile.getInputStream(entry).use { input ->
                    FileOutputStream(output, false).use { stream ->
                        val buffer = ByteArray(16 * 1024)
                        var read: Int
                        while (input.read(buffer).also { read = it } >= 0) {
                            if (read > 0) stream.write(buffer, 0, read)
                        }
                        stream.fd.sync()
                    }
                }
                materializedVideos[source] = output
                Log.i(
                    TAG,
                    "android.video.resource.materialized member=$source bytes=" + output.length()
                )
                Uri.fromFile(output)
            }
        } catch (failure: IOException) {
            Log.w(TAG, "android.video.resource.failed member=$source", failure)
            if (output.exists()) output.delete()
            null
        }
    }

    private fun isDescendant(candidateParent: View, candidateChild: View): Boolean {
        var current: View? = candidateParent
        while (current != null) {
            if (current === candidateChild) return true
            val parent = current.parent
            if (parent !is View) return false
            current = parent
        }
        return false
    }

    private fun logical(value: Double): Int {
        return max(0, (value.toFloat() * density).roundToInt())
    }

    private fun key(surfaceId: String, nodeId: String?): String {
        return surfaceId + '\u0000' + nodeId
    }

    private fun suppress(surfaceId: String?, nodeId: String?, value: Boolean) {
        val node = nodes[key(surfaceId ?: return, nodeId)]
        if (node != null) node.suppressEvents = value
    }

    private fun isSuppressed(surfaceId: String, nodeId: String): Boolean {
        val node = nodes[key(surfaceId, nodeId)]
        return node != null && node.suppressEvents
    }

    companion object {
        private const val TAG = "QuickAppKit"
        private const val COMPONENT_VIEW = 0
        private const val COMPONENT_TEXT = 1
        private const val COMPONENT_BUTTON = 2
        private const val COMPONENT_IMAGE = 3
        private const val COMPONENT_INPUT = 4
        private const val COMPONENT_SWITCH = 5
        private const val COMPONENT_SLIDER = 6
        private const val COMPONENT_PICKER = 7
        private const val COMPONENT_LIST = 8
        private const val COMPONENT_SCROLL = 9
        private const val COMPONENT_VIDEO = 10
        private const val COMPONENT_TABS = 11
    }
}
