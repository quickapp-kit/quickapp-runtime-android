package dev.quickapp.kit.android;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class MainActivity extends Activity {
    private static final String DEFAULT_RPK = "golden-app.rpk";

    private RuntimeBridge runtime;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        window.setStatusBarColor(Color.WHITE);
        window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.WHITE);
        setContentView(root);

        root.post(() -> {
            float density = getResources().getDisplayMetrics().density;
            float width = root.getWidth() / density;
            float height = root.getHeight() / density;
            String rpkPath = copyRuntimeRpk(selectedRpkAsset()).getAbsolutePath();
            runtime = new RuntimeBridge(this, root, width, height, rpkPath);
            runtime.start(rpkPath);
        });
    }

    @Override
    protected void onDestroy() {
        if (runtime != null) runtime.destroy();
        super.onDestroy();
    }

    private String selectedRpkAsset() {
        String requested = getIntent().getStringExtra("quickapp.rpk");
        if ("gallery-001.rpk".equals(requested) ||
                "consumer-001.rpk".equals(requested) ||
                "wearable-001.rpk".equals(requested) ||
                "commerce-001.rpk".equals(requested) ||
                "controls-001.rpk".equals(requested) ||
                "controls-002.rpk".equals(requested) ||
                "list-001.rpk".equals(requested) ||
                "platform-001.rpk".equals(requested) ||
                "media-001.rpk".equals(requested) ||
                "tabs-001.rpk".equals(requested) ||
                "capability-gallery-001.rpk".equals(requested)) {
            return requested;
        }
        return DEFAULT_RPK;
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
            throw new IllegalStateException("cannot install Runtime RPK", failure);
        }
    }
}
