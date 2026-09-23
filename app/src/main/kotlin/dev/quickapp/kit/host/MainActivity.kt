package dev.quickapp.kit.host

import android.annotation.SuppressLint
import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import dev.quickapp.kit.android.QuickAppResult
import dev.quickapp.kit.android.QuickAppRuntime
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import kotlin.math.roundToInt

class MainActivity : Activity() {
    private var root: FrameLayout? = null
    private var catalogScroll: ScrollView? = null
    private var catalog: LinearLayout? = null
    private var runtimeContainer: FrameLayout? = null
    private var runtimeSurface: FrameLayout? = null
    private var runtime: QuickAppRuntime? = null
    private var backCallback: OnBackInvokedCallback? = null
    private var exitRpkButton: TextView? = null

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val window = window
        window.statusBarColor = Color.WHITE
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR

        val content = FrameLayout(this)
        root = content
        content.setBackgroundColor(Color.WHITE)
        content.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val systemBars = insets.getInsets(
                    WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout()
                )
                view.setPadding(
                    systemBars.left, systemBars.top, systemBars.right, systemBars.bottom
                )
            } else {
                @Suppress("DEPRECATION")
                view.fitsSystemWindows = true
            }
            insets
        }
        setContentView(content)
        content.requestApplyInsets()
        if (Build.VERSION.SDK_INT >= 33) {
            val callback = OnBackInvokedCallback { handleBack() }
            backCallback = callback
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback
            )
        }
        showCatalog()
        if (availableRpkAssets().contains(DEFAULT_RPK_ASSET)) {
            openRuntime(DEFAULT_RPK_ASSET)
        }
    }

    @SuppressLint("GestureBackNavigation", "MissingSuperCall")
    @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
    override fun onBackPressed() {
        handleBack()
    }

    private fun handleBack() {
        if (runtime != null) {
            closeRuntime()
            return
        }
        @Suppress("DEPRECATION")
        super.onBackPressed()
    }

    override fun onDestroy() {
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback?.let {
                onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it)
            }
            backCallback = null
        }
        closeRuntime()
        super.onDestroy()
    }

    private fun dp(value: Int): Int {
        return (value * resources.displayMetrics.density).roundToInt()
    }

    private fun showCatalog() {
        val content = root ?: return
        catalogScroll?.let { content.removeView(it) }
        var container = runtimeContainer
        if (container == null) {
            container = FrameLayout(this)
            container.layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
            )
            content.addView(container)
            runtimeContainer = container
        }
        container.visibility = View.GONE

        val scroll = ScrollView(this)
        scroll.isFillViewport = true
        scroll.visibility = View.VISIBLE
        content.addView(
            scroll,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        catalogScroll = scroll

        val list = LinearLayout(this)
        list.orientation = LinearLayout.VERTICAL
        list.setPadding(48, 64, 48, 32)
        list.setBackgroundColor(Color.WHITE)
        scroll.addView(
            list,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )
        catalog = list

        val title = TextView(this)
        title.text = "QuickAppKitHost"
        title.setTextColor(Color.DKGRAY)
        title.textSize = 28f
        list.addView(
            title,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 100)
        )

        val subtitle = TextView(this)
        subtitle.text = "本地应用"
        subtitle.setTextColor(Color.GRAY)
        subtitle.textSize = 16f
        list.addView(
            subtitle,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 72)
        )

        val grid = LinearLayout(this)
        grid.orientation = LinearLayout.VERTICAL
        list.addView(
            grid,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        val availableRpkAssets = availableRpkAssets()
        var index = 0
        while (index < availableRpkAssets.size) {
            val row = LinearLayout(this)
            row.gravity = Gravity.TOP
            grid.addView(
                row,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
            var column = 0
            while (column < 3 && index + column < availableRpkAssets.size) {
                row.addView(
                    createRpkTile(availableRpkAssets[index + column]),
                    LinearLayout.LayoutParams(0, dp(160), 1f)
                )
                column++
            }
            index += 3
        }
    }

    private fun availableRpkAssets(): Array<String> {
        return try {
            val bundledAssets = assets.list("") ?: return arrayOf()
            val bundledAssetNames = bundledAssets.toHashSet()
            RPK_ASSETS.filter { bundledAssetNames.contains(it) }.toTypedArray()
        } catch (error: IOException) {
            arrayOf()
        }
    }

    private fun createRpkTile(assetName: String): View {
        val tile = LinearLayout(this)
        tile.orientation = LinearLayout.VERTICAL
        tile.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        tile.setPadding(dp(6), dp(8), dp(6), dp(8))
        tile.setOnClickListener { openRuntime(assetName) }

        val icon = FrameLayout(this)
        val iconBackground = GradientDrawable()
        iconBackground.setColor(Color.rgb(20, 145, 242))
        iconBackground.cornerRadius = dp(16).toFloat()
        icon.background = iconBackground
        val inner = TextView(this)
        inner.background = createWhiteIconShape()
        val innerParams = FrameLayout.LayoutParams(dp(30), dp(30))
        innerParams.gravity = Gravity.CENTER
        icon.addView(inner, innerParams)
        tile.addView(icon, LinearLayout.LayoutParams(dp(72), dp(72)))

        val label = TextView(this)
        label.text = displayName(assetName)
        label.setTextColor(Color.rgb(30, 30, 30))
        label.textSize = 15f
        label.gravity = Gravity.CENTER
        label.maxLines = 2
        tile.addView(
            label,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44))
        )
        return tile
    }

    private fun createWhiteIconShape(): GradientDrawable {
        val shape = GradientDrawable()
        shape.setColor(Color.WHITE)
        shape.cornerRadius = dp(10).toFloat()
        return shape
    }

    private fun displayName(assetName: String): String {
        return assetName.substring(0, assetName.length - 4)
    }

    private fun compactName(assetName: String): String {
        return assetName.substring(0, assetName.length - 4)
    }

    private fun openRuntime(assetName: String) {
        if (runtime != null) closeRuntime()
        val container = runtimeContainer ?: return
        container.visibility = View.VISIBLE
        catalog?.visibility = View.GONE
        catalogScroll?.visibility = View.GONE
        container.removeAllViews()

        val header = LinearLayout(this)
        header.orientation = LinearLayout.HORIZONTAL
        header.gravity = Gravity.CENTER_VERTICAL
        header.setPadding(dp(16), 0, dp(16), 0)
        header.setBackgroundColor(Color.WHITE)
        container.addView(
            header,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(68), Gravity.TOP
            )
        )

        val exitButton = TextView(this)
        exitButton.text = "↪  退出 RPK"
        exitButton.setTextColor(Color.rgb(0, 122, 255))
        exitButton.textSize = 17f
        exitButton.gravity = Gravity.CENTER_VERTICAL
        exitButton.contentDescription = "退出当前 RPK，返回应用列表"
        exitButton.setOnClickListener { closeRuntime() }
        exitRpkButton = exitButton
        header.addView(
            exitButton,
            LinearLayout.LayoutParams(dp(128), ViewGroup.LayoutParams.MATCH_PARENT)
        )

        val appTitle = TextView(this)
        appTitle.text = compactName(assetName)
        appTitle.setTextColor(Color.rgb(30, 30, 30))
        appTitle.textSize = 19f
        appTitle.gravity = Gravity.CENTER
        appTitle.isSingleLine = true
        header.addView(
            appTitle,
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f)
        )

        val loadedState = TextView(this)
        loadedState.text = "已加载 (app.name)"
        loadedState.setTextColor(Color.GRAY)
        loadedState.textSize = 16f
        loadedState.gravity = Gravity.CENTER_VERTICAL or Gravity.END
        header.addView(
            loadedState,
            LinearLayout.LayoutParams(dp(140), ViewGroup.LayoutParams.MATCH_PARENT)
        )

        val surface = FrameLayout(this)
        surface.setBackgroundColor(Color.WHITE)
        val surfaceParams = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
        )
        surfaceParams.topMargin = dp(68)
        container.addView(surface, surfaceParams)
        runtimeSurface = surface

        surface.post {
            if (runtimeSurface == null || runtime != null) return@post
            val created = QuickAppRuntime.create(this)
            runtime = created
            val attached = created.attachSurface(surface)
            if (!attached.isSuccess) {
                showFailure(attached)
                return@post
            }
            val rpk = copyRuntimeRpk(assetName)
            val loaded = if (rpk == null) {
                QuickAppResult.failed("RPK_COPY_FAILED", "Cannot copy RPK into private storage")
            } else {
                created.loadRpk(rpk)
            }
            if (!loaded.isSuccess) showFailure(loaded)
        }
    }

    private fun closeRuntime() {
        runtime?.let {
            it.destroy()
            runtime = null
        }
        exitRpkButton?.let {
            (it.parent as? ViewGroup)?.removeView(it)
            exitRpkButton = null
        }
        runtimeContainer?.let {
            it.removeAllViews()
            runtimeSurface = null
        }
        if (root != null && !isFinishing) showCatalog()
    }

    private fun showFailure(result: QuickAppResult) {
        closeRuntime()
        val failure = TextView(this)
        failure.text = "Runtime failed: " + result.errorCode + " " + result.message
        failure.setTextColor(Color.RED)
        catalog?.addView(
            failure,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 120)
        )
    }

    private fun copyRuntimeRpk(assetName: String): File? {
        val output = File(filesDir, assetName)
        return try {
            assets.open(assetName).use { input ->
                FileOutputStream(output, false).use { stream ->
                    val buffer = ByteArray(16 * 1024)
                    var read: Int
                    while (input.read(buffer).also { read = it } >= 0) {
                        if (read > 0) stream.write(buffer, 0, read)
                    }
                    stream.fd.sync()
                }
            }
            output
        } catch (failure: IOException) {
            null
        }
    }

    companion object {
        private const val DEFAULT_RPK_ASSET = "shop.rpk"
        private val RPK_ASSETS = arrayOf(
            "tk-s07-case001.rpk",
            "tk-s08-binding001.rpk",
            "tk-s09-case002.rpk",
            "tk-s10-block001.rpk",
            "tk-s11-image-input001.rpk",
            "tk-s12-lvgl-p0.rpk",
            "tk-timer-001.rpk",
            "inspection-board.rpk",
            "content-hub.rpk",
            "health-summary.rpk",
            "controls-001.rpk",
            "controls-002.rpk",
            "list-001.rpk",
            "long-list-001.rpk",
            "media-001.rpk",
            "platform-001.rpk",
            "tabs-001.rpk",
            "capability-gallery-001.rpk",
            "shop.rpk",
            "sport-band.rpk",
            "sport-watch.rpk",
            "url-001.rpk",
            "card-wallet.rpk"
        )
    }
}
