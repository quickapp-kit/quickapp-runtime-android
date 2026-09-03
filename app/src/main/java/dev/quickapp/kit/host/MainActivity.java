package dev.quickapp.kit.host;

import android.app.Activity;
import android.annotation.SuppressLint;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.graphics.drawable.GradientDrawable;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import dev.quickapp.kit.android.QuickAppResult;
import dev.quickapp.kit.android.QuickAppRuntime;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class MainActivity extends Activity {
    private static final String[] RPK_ASSETS = {
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
    };

    private FrameLayout root;
    private ScrollView catalogScroll;
    private LinearLayout catalog;
    private FrameLayout runtimeContainer;
    private FrameLayout runtimeSurface;
    private QuickAppRuntime runtime;
    private OnBackInvokedCallback backCallback;
    private TextView exitRpkButton;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        window.setStatusBarColor(Color.WHITE);
        window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);

        root = new FrameLayout(this);
        root.setBackgroundColor(Color.WHITE);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets systemBars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(systemBars.left, systemBars.top,
                        systemBars.right, systemBars.bottom);
            } else {
                view.setFitsSystemWindows(true);
            }
            return insets;
        });
        setContentView(root);
        root.requestApplyInsets();
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = this::handleBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
        showCatalog();
    }

    @Override
    @SuppressLint("GestureBackNavigation")
    public void onBackPressed() {
        handleBack();
    }

    private void handleBack() {
        if (runtime != null) {
            closeRuntime();
            return;
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= 33 && backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        closeRuntime();
        super.onDestroy();
    }

    private void showCatalog() {
        if (catalogScroll != null) root.removeView(catalogScroll);
        if (runtimeContainer == null) {
            runtimeContainer = new FrameLayout(this);
            runtimeContainer.setLayoutParams(new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            root.addView(runtimeContainer);
        }
        runtimeContainer.setVisibility(View.GONE);

        catalogScroll = new ScrollView(this);
        catalogScroll.setFillViewport(true);
        catalogScroll.setVisibility(View.VISIBLE);
        root.addView(catalogScroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        catalog = new LinearLayout(this);
        catalog.setOrientation(LinearLayout.VERTICAL);
        catalog.setPadding(48, 64, 48, 32);
        catalog.setBackgroundColor(Color.WHITE);
        catalogScroll.addView(catalog, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText("QuickAppKitHost");
        title.setTextColor(Color.DKGRAY);
        title.setTextSize(28);
        catalog.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 100));

        TextView subtitle = new TextView(this);
        subtitle.setText("本地应用");
        subtitle.setTextColor(Color.GRAY);
        subtitle.setTextSize(16);
        catalog.addView(subtitle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 72));

        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.VERTICAL);
        catalog.addView(grid, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        for (int index = 0; index < RPK_ASSETS.length; index += 3) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(android.view.Gravity.TOP);
            grid.addView(row, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            for (int column = 0; column < 3 && index + column < RPK_ASSETS.length; column++) {
                row.addView(createRpkTile(RPK_ASSETS[index + column]), new LinearLayout.LayoutParams(
                        0, dp(160), 1));
            }
        }
    }

    private View createRpkTile(String assetName) {
        LinearLayout tile = new LinearLayout(this);
        tile.setOrientation(LinearLayout.VERTICAL);
        tile.setGravity(android.view.Gravity.TOP | android.view.Gravity.CENTER_HORIZONTAL);
        tile.setPadding(dp(6), dp(8), dp(6), dp(8));
        tile.setOnClickListener(view -> openRuntime(assetName));

        FrameLayout icon = new FrameLayout(this);
        GradientDrawable iconBackground = new GradientDrawable();
        iconBackground.setColor(Color.rgb(20, 145, 242));
        iconBackground.setCornerRadius(dp(16));
        icon.setBackground(iconBackground);
        TextView inner = new TextView(this);
        inner.setBackground(createWhiteIconShape());
        FrameLayout.LayoutParams innerParams = new FrameLayout.LayoutParams(dp(30), dp(30));
        innerParams.gravity = android.view.Gravity.CENTER;
        icon.addView(inner, innerParams);
        tile.addView(icon, new LinearLayout.LayoutParams(dp(72), dp(72)));

        TextView label = new TextView(this);
        label.setText(displayName(assetName));
        label.setTextColor(Color.rgb(30, 30, 30));
        label.setTextSize(15);
        label.setGravity(android.view.Gravity.CENTER);
        label.setMaxLines(2);
        tile.addView(label, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(44)));
        return tile;
    }

    private GradientDrawable createWhiteIconShape() {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(Color.WHITE);
        shape.setCornerRadius(dp(10));
        return shape;
    }

    private String displayName(String assetName) {
        return assetName.substring(0, assetName.length() - 4);
    }

    private String compactName(String assetName) {
        return assetName.substring(0, assetName.length() - 4);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void openRuntime(String assetName) {
        if (runtime != null) closeRuntime();
        runtimeContainer.setVisibility(View.VISIBLE);
        catalog.setVisibility(View.GONE);
        catalogScroll.setVisibility(View.GONE);
        runtimeContainer.removeAllViews();

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(android.view.Gravity.CENTER_VERTICAL);
        header.setPadding(dp(16), 0, dp(16), 0);
        header.setBackgroundColor(Color.WHITE);
        runtimeContainer.addView(header, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(68), android.view.Gravity.TOP));

        exitRpkButton = new TextView(this);
        exitRpkButton.setText("↪  退出 RPK");
        exitRpkButton.setTextColor(Color.rgb(0, 122, 255));
        exitRpkButton.setTextSize(17);
        exitRpkButton.setGravity(android.view.Gravity.CENTER_VERTICAL);
        exitRpkButton.setContentDescription("退出当前 RPK，返回应用列表");
        exitRpkButton.setOnClickListener(view -> closeRuntime());
        header.addView(exitRpkButton, new LinearLayout.LayoutParams(
                dp(128), ViewGroup.LayoutParams.MATCH_PARENT));

        TextView appTitle = new TextView(this);
        appTitle.setText(compactName(assetName));
        appTitle.setTextColor(Color.rgb(30, 30, 30));
        appTitle.setTextSize(19);
        appTitle.setGravity(android.view.Gravity.CENTER);
        appTitle.setSingleLine(true);
        header.addView(appTitle, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1));

        TextView loadedState = new TextView(this);
        loadedState.setText("已加载 (app.name)");
        loadedState.setTextColor(Color.GRAY);
        loadedState.setTextSize(16);
        loadedState.setGravity(android.view.Gravity.CENTER_VERTICAL | android.view.Gravity.END);
        header.addView(loadedState, new LinearLayout.LayoutParams(
                dp(140), ViewGroup.LayoutParams.MATCH_PARENT));

        runtimeSurface = new FrameLayout(this);
        runtimeSurface.setBackgroundColor(Color.WHITE);
        FrameLayout.LayoutParams surfaceParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        surfaceParams.topMargin = dp(68);
        runtimeContainer.addView(runtimeSurface, surfaceParams);

        runtime = QuickAppRuntime.create(this);
        QuickAppResult attached = runtime.attachSurface(runtimeSurface);
        if (!attached.isSuccess()) {
            showFailure(attached);
            return;
        }
        File rpk = copyRuntimeRpk(assetName);
        QuickAppResult loaded = rpk == null
                ? QuickAppResult.failed("RPK_COPY_FAILED", "Cannot copy RPK into private storage")
                : runtime.loadRpk(rpk);
        if (!loaded.isSuccess()) showFailure(loaded);
    }

    private void closeRuntime() {
        if (runtime != null) {
            runtime.destroy();
            runtime = null;
        }
        if (exitRpkButton != null && exitRpkButton.getParent() instanceof ViewGroup) {
            ((ViewGroup) exitRpkButton.getParent()).removeView(exitRpkButton);
            exitRpkButton = null;
        }
        if (runtimeContainer != null) {
            runtimeContainer.removeAllViews();
            runtimeSurface = null;
        }
        if (root != null && !isFinishing()) showCatalog();
    }

    private void showFailure(QuickAppResult result) {
        closeRuntime();
        TextView failure = new TextView(this);
        failure.setText("Runtime failed: " + result.errorCode + " " + result.message);
        failure.setTextColor(Color.RED);
        catalog.addView(failure, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 120));
    }

    private File copyRuntimeRpk(String assetName) {
        File output = new File(getFilesDir(), assetName);
        try (InputStream input = getAssets().open(assetName);
             FileOutputStream stream = new FileOutputStream(output, false)) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0) stream.write(buffer, 0, read);
            }
            stream.getFD().sync();
            return output;
        } catch (IOException failure) {
            return null;
        }
    }
}
