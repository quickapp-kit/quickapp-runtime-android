package dev.quickapp.kit.android;

import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.MediaController;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.VideoView;
import android.net.Uri;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipFile;
import java.util.HashMap;
import java.util.Map;

final class RuntimeSurfaceHost {
    private static final String TAG = "QuickAppKit";
    private static final int COMPONENT_VIEW = 0;
    private static final int COMPONENT_TEXT = 1;
    private static final int COMPONENT_BUTTON = 2;
    private static final int COMPONENT_IMAGE = 3;
    private static final int COMPONENT_INPUT = 4;
    private static final int COMPONENT_SWITCH = 5;
    private static final int COMPONENT_SLIDER = 6;
    private static final int COMPONENT_PICKER = 7;
    private static final int COMPONENT_LIST = 8;
    private static final int COMPONENT_SCROLL = 9;
    private static final int COMPONENT_VIDEO = 10;
    private static final int COMPONENT_TABS = 11;

    static final class EventPayload {
        String stringValue;
        String numberName = "value";
        double numberValue;
        boolean hasNumber;
        boolean booleanValue;
        boolean hasBoolean;
        boolean fromUser;
        boolean hasFromUser;
        double scrollOffset;
        double contentSize;
        double viewportSize;
        boolean hasScrollMetrics;
    }

    interface EventSink {
        void onEvent(String surfaceId, String nodeId, String eventType,
                     EventPayload payload,
                     long timestampNs);
    }

    private static final class NodeRecord {
        final String surfaceId;
        final View view;
        int backgroundColor = Color.TRANSPARENT;
        float borderRadius;
        boolean suppressEvents;
        double sliderMin;
        double sliderMax;
        double sliderStep = 1;
        String[] pickerOptions = new String[0];
        int pickerSelected;
        boolean atTop;
        boolean atBottom;
        Runnable scrollEndRunnable;
        VideoView videoView;
        ImageView videoPoster;
        android.media.MediaPlayer videoPlayer;
        Runnable videoProgressRunnable;
        TextView videoTimeLabel;
        SeekBar videoProgressBar;
        ImageButton videoPlayButton;
        MediaController videoController;
        boolean videoAutoplay;
        boolean videoControls;
        boolean videoMuted;
        boolean videoPlayRequested;
        LinearLayout tabsView;
        String[] tabsItems = new String[0];
        int tabsSelected;
        int tabsTextColor = Color.WHITE;

        NodeRecord(String surfaceId, View view) {
            this.surfaceId = surfaceId;
            this.view = view;
        }
    }

    private final FrameLayout appRoot;
    private final EventSink eventSink;
    private final float density;
    private String rpkPath;
    private final Map<String, FrameLayout> surfaces = new HashMap<>();
    private final Map<String, NodeRecord> nodes = new HashMap<>();
    private final Map<String, File> materializedVideos = new HashMap<>();

    RuntimeSurfaceHost(FrameLayout appRoot, EventSink eventSink, String rpkPath) {
        this.appRoot = appRoot;
        this.eventSink = eventSink;
        this.density = appRoot.getResources().getDisplayMetrics().density;
        this.rpkPath = rpkPath;
        appRoot.setFocusableInTouchMode(true);
    }

    void setRpkPath(String rpkPath) {
        this.rpkPath = rpkPath;
    }

    boolean dispatchInput(int action, float x, float y, long timestampNs) {
        long eventTimeMs = timestampNs > 0 ? timestampNs / 1_000_000L : System.currentTimeMillis();
        android.view.MotionEvent event = android.view.MotionEvent.obtain(
                eventTimeMs, eventTimeMs, action, x, y, 0);
        try {
            return appRoot.dispatchTouchEvent(event);
        } finally {
            event.recycle();
        }
    }

    boolean createSurface(String surfaceId) {
        if (surfaceId == null || surfaces.containsKey(surfaceId)) {
            return false;
        }
        FrameLayout container = new FrameLayout(appRoot.getContext());
        container.setVisibility(View.INVISIBLE);
        container.setBackgroundColor(Color.WHITE);
        appRoot.addView(container, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        surfaces.put(surfaceId, container);
        return true;
    }

    boolean presentRoot(String targetSurfaceId) {
        FrameLayout target = surfaces.get(targetSurfaceId);
        if (target == null) {
            return false;
        }
        target.setVisibility(View.VISIBLE);
        return true;
    }

    boolean presentPush(String sourceSurfaceId, String targetSurfaceId) {
        FrameLayout source = surfaces.get(sourceSurfaceId);
        FrameLayout target = surfaces.get(targetSurfaceId);
        if (source == null || target == null || source.getVisibility() != View.VISIBLE) {
            return false;
        }
        source.setVisibility(View.INVISIBLE);
        target.setVisibility(View.VISIBLE);
        return true;
    }

    boolean setVisible(String surfaceId, boolean visible) {
        FrameLayout surface = surfaces.get(surfaceId);
        if (surface == null) {
            return false;
        }
        surface.setVisibility(visible ? View.VISIBLE : View.INVISIBLE);
        return true;
    }

    boolean closeAndReveal(String sourceSurfaceId, String revealSurfaceId) {
        FrameLayout source = surfaces.get(sourceSurfaceId);
        FrameLayout reveal = surfaces.get(revealSurfaceId);
        if (source == null || reveal == null) {
            return false;
        }
        // Core sends a separate DestroySurface after close/onDestroy. Keep the
        // platform surface registered until that command arrives.
        source.setVisibility(View.INVISIBLE);
        reveal.setVisibility(View.VISIBLE);
        return true;
    }

    boolean destroySurface(String surfaceId) {
        if (!surfaces.containsKey(surfaceId)) {
            return false;
        }
        removeSurface(surfaceId);
        return true;
    }

    boolean apply(MountTransaction transaction) {
        FrameLayout surface = surfaces.get(transaction.surfaceId);
        if (surface == null || transaction.operations == null) {
            return false;
        }
        if (transaction.full) {
            clearSurface(transaction.surfaceId);
        }
        try {
            for (MountOperation operation : transaction.operations) {
                if (!applyOperation(transaction.surfaceId, surface, operation)) {
                    Log.e(TAG, "android.mount.operation.failed surface=" + transaction.surfaceId +
                            " kind=" + (operation == null ? "null" : operation.kind) +
                            " node=" + (operation == null ? "" : operation.nodeId) +
                            " property=" + (operation == null ? "" : operation.propertyName));
                    return false;
                }
            }
            return true;
        } catch (RuntimeException failure) {
            Log.e(TAG, "android.mount.exception surface=" + transaction.surfaceId, failure);
            return false;
        }
    }

    int surfaceCount() {
        return surfaces.size();
    }

    int nodeCount() {
        return nodes.size();
    }

    void close() {
        for (FrameLayout surface : surfaces.values()) {
            appRoot.removeView(surface);
        }
        nodes.clear();
        surfaces.clear();
        for (File video : materializedVideos.values()) {
            if (video != null) video.delete();
        }
        materializedVideos.clear();
    }

    private boolean applyOperation(
            String surfaceId, FrameLayout surface, MountOperation operation) {
        if (operation == null || operation.nodeId == null) {
            return false;
        }
        switch (operation.kind) {
            case MountOperation.CREATE:
                return createNode(surfaceId, surface, operation);
            case MountOperation.SET_PROP:
                return setProperty(surfaceId, operation);
            case MountOperation.SET_LAYOUT:
                return setLayout(surfaceId, operation);
            case MountOperation.INSERT:
            case MountOperation.MOVE:
                return moveNode(surfaceId, operation);
            case MountOperation.REMOVE:
                return removeNode(surfaceId, operation.nodeId);
            default:
                return false;
        }
    }

    private boolean createNode(
            String surfaceId, FrameLayout surface, MountOperation operation) {
        String key = key(surfaceId, operation.nodeId);
        if (nodes.containsKey(key)) {
            return false;
        }
        View view;
        if (operation.componentType == COMPONENT_TEXT) {
            TextView text = new TextView(appRoot.getContext());
            text.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            view = text;
        } else if (operation.componentType == COMPONENT_BUTTON) {
            Button button = new Button(appRoot.getContext());
            button.setAllCaps(false);
            button.setGravity(Gravity.CENTER);
            button.setPadding(0, 0, 0, 0);
            button.setOnTouchListener((ignored, event) -> {
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    appRoot.requestFocus();
                }
                return false;
            });
            button.setOnClickListener(ignored -> {
                Log.i(TAG, "android.input.click surface=" + surfaceId +
                        " node=" + operation.nodeId);
                eventSink.onEvent(surfaceId, operation.nodeId, "click",
                        new EventPayload(), android.os.SystemClock.elapsedRealtimeNanos());
            });
            view = button;
        } else if (operation.componentType == COMPONENT_VIEW) {
            view = new FrameLayout(appRoot.getContext());
        } else if (operation.componentType == COMPONENT_IMAGE) {
            ImageView image = new ImageView(appRoot.getContext());
            image.setScaleType(ImageView.ScaleType.CENTER_INSIDE);
            view = image;
        } else if (operation.componentType == COMPONENT_INPUT) {
            EditText input = new EditText(appRoot.getContext());
            input.setSingleLine(true);
            input.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int start,
                                                         int count, int after) {}
                @Override public void onTextChanged(CharSequence s, int start,
                                                     int before, int count) {
                    if (!isSuppressed(surfaceId, operation.nodeId)) {
                        EventPayload payload = new EventPayload();
                        payload.stringValue = s == null ? "" : s.toString();
                        eventSink.onEvent(surfaceId, operation.nodeId, "input", payload,
                                android.os.SystemClock.elapsedRealtimeNanos());
                    }
                }
                @Override public void afterTextChanged(Editable s) {}
            });
            input.setOnFocusChangeListener((ignored, focused) -> {
                if (!isSuppressed(surfaceId, operation.nodeId)) {
                    EventPayload payload = new EventPayload();
                    payload.stringValue = input.getText().toString();
                    eventSink.onEvent(surfaceId, operation.nodeId,
                            focused ? "focus" : "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos());
                }
            });
            view = input;
        } else if (operation.componentType == COMPONENT_SWITCH) {
            Switch toggle = new Switch(appRoot.getContext());
            toggle.setOnTouchListener((ignored, event) -> {
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    appRoot.requestFocus();
                }
                return false;
            });
            toggle.setOnCheckedChangeListener((ignored, checked) -> {
                if (!isSuppressed(surfaceId, operation.nodeId)) {
                    EventPayload payload = new EventPayload();
                    payload.booleanValue = checked;
                    payload.hasBoolean = true;
                    eventSink.onEvent(surfaceId, operation.nodeId, "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos());
                }
            });
            view = toggle;
        } else if (operation.componentType == COMPONENT_SLIDER) {
            SeekBar slider = new SeekBar(appRoot.getContext());
            slider.setOnTouchListener((ignored, event) -> {
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    appRoot.requestFocus();
                }
                return false;
            });
            slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar bar, int progress,
                                                          boolean fromUser) {
                    if (!fromUser || isSuppressed(surfaceId, operation.nodeId)) return;
                    NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
                    if (node == null) return;
                    EventPayload payload = new EventPayload();
                    payload.numberValue = sliderValue(node, progress);
                    payload.hasNumber = true;
                    payload.fromUser = true;
                    payload.hasFromUser = true;
                    Log.i(TAG, "android.input.slider.change surface=" + surfaceId +
                            " node=" + operation.nodeId + " value=" + payload.numberValue +
                            " isFromUser=true");
                    eventSink.onEvent(surfaceId, operation.nodeId, "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos());
                }
                @Override public void onStartTrackingTouch(SeekBar bar) {}
                @Override public void onStopTrackingTouch(SeekBar bar) {}
            });
            view = slider;
        } else if (operation.componentType == COMPONENT_PICKER) {
            TextView picker = new TextView(appRoot.getContext());
            picker.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            picker.setClickable(true);
            picker.setOnTouchListener((ignored, event) -> {
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    appRoot.requestFocus();
                }
                return false;
            });
            picker.setOnClickListener(ignored -> showPicker(surfaceId, operation.nodeId, picker));
            view = picker;
        } else if (operation.componentType == COMPONENT_LIST) {
            FrameLayout list = new FrameLayout(appRoot.getContext());
            list.setClipChildren(false);
            view = list;
        } else if (operation.componentType == COMPONENT_SCROLL) {
            ScrollView scroll = new ScrollView(appRoot.getContext());
            scroll.setFillViewport(false);
            scroll.setClipToPadding(true);
            scroll.setOnScrollChangeListener((changed, scrollX, scrollY, oldScrollX, oldScrollY) -> {
                NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
                if (node == null || node.suppressEvents || scrollY == oldScrollY) return;
                emitScroll(surfaceId, operation.nodeId, scroll, node, "scroll");
                boolean atTop = scrollY <= 0;
                boolean atBottom = scrollY >= scrollRange(scroll);
                if (atTop && !node.atTop) {
                    emitScroll(surfaceId, operation.nodeId, scroll, node, "scrolltop");
                }
                if (atBottom && !node.atBottom) {
                    emitScroll(surfaceId, operation.nodeId, scroll, node, "scrollbottom");
                }
                node.atTop = atTop;
                node.atBottom = atBottom;
                if (node.scrollEndRunnable != null) scroll.removeCallbacks(node.scrollEndRunnable);
                node.scrollEndRunnable = () -> {
                    if (!node.suppressEvents && nodes.containsKey(key(surfaceId, operation.nodeId))) {
                        emitScroll(surfaceId, operation.nodeId, scroll, node, "scrollend");
                    }
                };
                scroll.postDelayed(node.scrollEndRunnable, 120L);
            });
            view = scroll;
        } else if (operation.componentType == COMPONENT_TABS) {
            LinearLayout tabs = new LinearLayout(appRoot.getContext());
            tabs.setOrientation(LinearLayout.HORIZONTAL);
            tabs.setGravity(Gravity.CENTER_VERTICAL);
            tabs.setBaselineAligned(false);
            tabs.setClipToOutline(true);
            view = tabs;
        } else if (operation.componentType == COMPONENT_VIDEO) {
            FrameLayout container = new FrameLayout(appRoot.getContext());
            container.setBackgroundColor(Color.rgb(32, 37, 43));
            ImageView poster = new ImageView(appRoot.getContext());
            poster.setScaleType(ImageView.ScaleType.CENTER_INSIDE);
            poster.setBackgroundColor(Color.rgb(32, 37, 43));
            container.addView(poster, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            VideoView video = new VideoView(appRoot.getContext());
            video.setBackgroundColor(Color.TRANSPARENT);
            video.setAlpha(0f);
            container.addView(video, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            poster.bringToFront();
            video.setOnPreparedListener(player -> {
                NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
                if (node == null) return;
                node.videoPlayer = player;
                if (node.videoMuted) player.setVolume(0f, 0f);
                node.videoView.setVisibility(View.VISIBLE);
                node.videoView.setAlpha(1f);
                node.videoPoster.setVisibility(View.INVISIBLE);
                node.videoView.bringToFront();
                Log.i(TAG, "android.video.prepared surface=" + surfaceId +
                        " node=" + operation.nodeId);
                emitVideoEvent(surfaceId, operation.nodeId, "prepared", null);
                scheduleVideoTimeUpdates(surfaceId, operation.nodeId, video, node);
                if (node.videoAutoplay || node.videoPlayRequested) {
                    node.videoPlayRequested = false;
                    video.start();
                    Log.i(TAG, "android.video.start surface=" + surfaceId +
                            " node=" + operation.nodeId + " autoplay=" + node.videoAutoplay);
                    emitVideoEvent(surfaceId, operation.nodeId, "start", null);
                }
            });
            video.setOnCompletionListener(player -> {
                stopVideoTimeUpdates(surfaceId, operation.nodeId, video);
                Log.i(TAG, "android.video.finish surface=" + surfaceId +
                        " node=" + operation.nodeId);
                emitVideoEvent(surfaceId, operation.nodeId, "finish", null);
            });
            video.setOnErrorListener((player, what, extra) -> {
                stopVideoTimeUpdates(surfaceId, operation.nodeId, video);
                video.setVisibility(View.INVISIBLE);
                video.setAlpha(0f);
                NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
                if (node != null && node.videoPoster != null) {
                    node.videoPoster.setVisibility(View.VISIBLE);
                    node.videoPoster.bringToFront();
                }
                Log.i(TAG, "android.video.error surface=" + surfaceId +
                        " node=" + operation.nodeId + " what=" + what +
                        " extra=" + extra);
                emitVideoEvent(surfaceId, operation.nodeId, "error", null);
                return true;
            });
            video.setOnClickListener(ignored -> {
                NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
                if (node == null) return;
                if (node.videoPlayer == null) {
                    node.videoPlayRequested = true;
                    Log.i(TAG, "android.video.play.pending surface=" + surfaceId +
                            " node=" + operation.nodeId);
                    if (node.videoController != null) node.videoController.show();
                    return;
                }
                if (video.isPlaying()) {
                    video.pause();
                    emitVideoEvent(surfaceId, operation.nodeId, "pause", null);
                } else {
                    video.start();
                    emitVideoEvent(surfaceId, operation.nodeId, "start", null);
                }
                if (node.videoController != null) node.videoController.show();
            });
            poster.setOnClickListener(ignored -> video.performClick());
            view = container;
        } else {
            return false;
        }
        view.setTag(operation.nodeId);
        surface.addView(view, new FrameLayout.LayoutParams(0, 0));
        NodeRecord record = new NodeRecord(surfaceId, view);
        if (operation.componentType == COMPONENT_SCROLL) {
            record.atTop = true;
            record.atBottom = false;
        }
        if (operation.componentType == COMPONENT_VIDEO) {
            ViewGroup container = (ViewGroup) view;
            for (int index = 0; index < container.getChildCount(); index++) {
                View child = container.getChildAt(index);
                if (child instanceof ImageView) {
                    record.videoPoster = (ImageView) child;
                } else if (child instanceof VideoView) {
                    record.videoView = (VideoView) child;
                }
            }
            if (record.videoPoster == null || record.videoView == null) {
                return false;
            }
        }
        if (operation.componentType == COMPONENT_TABS) {
            record.tabsView = (LinearLayout) view;
        }
        nodes.put(key, record);
        return true;
    }

    private boolean setProperty(String surfaceId, MountOperation operation) {
        NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
        if (node == null || operation.propertyName == null) {
            return false;
        }
        View view = node.view;
        if (node.videoView != null) {
            Log.i(TAG, "android.video.property node=" + operation.nodeId +
                    " name=" + operation.propertyName +
                    " valueKind=" + operation.valueKind);
        }
        switch (operation.propertyName) {
            case "text":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                if (view instanceof TextView) {
                    ((TextView) view).setText(operation.stringValue);
                    return true;
                }
                return false;
            case "items":
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                        node.tabsView == null || operation.stringValue == null) return false;
                node.tabsItems = operation.stringValue.split("\\|", -1);
                if (node.tabsItems.length == 0) return false;
                node.tabsSelected = clampTabsIndex(node.tabsSelected, node.tabsItems.length);
                rebuildTabs(surfaceId, operation.nodeId, node);
                return true;
            case "src":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                if (view instanceof ImageView) {
                    return loadImage((ImageView) view, operation.stringValue);
                }
                if (node.videoView != null) {
                    try {
                        Uri source = videoUri(operation.stringValue);
                        if (source == null) return false;
                        Log.i(TAG, "android.video.set_uri node=" + operation.nodeId +
                                " uri=" + source);
                        node.videoView.setVideoURI(source);
                        return true;
                    } catch (RuntimeException failure) {
                        Log.w(TAG, "android.video.source.failed node=" + operation.nodeId,
                                failure);
                        return false;
                    }
                }
                return false;
            case "poster":
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                        node.videoPoster == null) return false;
                return operation.stringValue == null || operation.stringValue.isEmpty() ||
                        loadImage(node.videoPoster, operation.stringValue);
            case "autoplay":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN ||
                        node.videoView == null) return false;
                node.videoAutoplay = operation.booleanValue;
                if (node.videoAutoplay && node.videoPlayer != null &&
                        !node.videoView.isPlaying()) {
                    node.videoView.start();
                    emitVideoEvent(surfaceId, operation.nodeId, "start", null);
                }
                return true;
            case "controls":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN ||
                        node.videoView == null) return false;
                node.videoControls = operation.booleanValue;
                node.videoView.setMediaController(null);
                node.videoController = null;
                if (!node.videoControls) return true;
                MediaController controls = new MediaController(appRoot.getContext());
                controls.setAnchorView(node.videoView);
                node.videoController = controls;
                node.videoView.setMediaController(controls);
                return true;
            case "muted":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN ||
                        node.videoView == null) return false;
                node.videoMuted = operation.booleanValue;
                if (node.videoPlayer != null) {
                    float volume = node.videoMuted ? 0f : 1f;
                    node.videoPlayer.setVolume(volume, volume);
                }
                return true;
            case "value":
                if (view instanceof EditText) {
                    if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                    suppress(surfaceId, operation.nodeId, true);
                    try {
                        ((EditText) view).setText(operation.stringValue);
                    } finally {
                        suppress(surfaceId, operation.nodeId, false);
                    }
                    return true;
                }
                if (view instanceof SeekBar) {
                    if (operation.valueKind != MountOperation.VALUE_NUMBER) return false;
                    updateSlider((SeekBar) view, node, node.sliderMin, node.sliderMax,
                            node.sliderStep, operation.numberValue);
                    return true;
                }
                return false;
            case "checked":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN ||
                        !(view instanceof Switch)) return false;
                suppress(surfaceId, operation.nodeId, true);
                try {
                    ((Switch) view).setChecked(operation.booleanValue);
                } finally {
                    suppress(surfaceId, operation.nodeId, false);
                }
                return true;
            case "min":
                if (operation.valueKind != MountOperation.VALUE_NUMBER ||
                        !(view instanceof SeekBar)) return false;
                node.sliderMin = operation.numberValue;
                updateSlider((SeekBar) view, node, node.sliderMin, node.sliderMax,
                        node.sliderStep, null);
                return true;
            case "max":
                if (operation.valueKind != MountOperation.VALUE_NUMBER ||
                        !(view instanceof SeekBar)) return false;
                node.sliderMax = operation.numberValue;
                updateSlider((SeekBar) view, node, node.sliderMin, node.sliderMax,
                        node.sliderStep, null);
                return true;
            case "step":
                if (operation.valueKind != MountOperation.VALUE_NUMBER ||
                        !(view instanceof SeekBar) || operation.numberValue <= 0) return false;
                node.sliderStep = operation.numberValue;
                updateSlider((SeekBar) view, node, node.sliderMin, node.sliderMax,
                        node.sliderStep, null);
                return true;
            case "range":
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                        !(view instanceof TextView)) return false;
                node.pickerOptions = operation.stringValue == null
                        ? new String[0] : operation.stringValue.split("\\|", -1);
                updatePickerText((TextView) view, node);
                return true;
            case "selected":
                if (operation.valueKind != MountOperation.VALUE_NUMBER) return false;
                if (node.tabsView != null) {
                    node.tabsSelected = clampTabsIndex(
                            (int) Math.round(operation.numberValue), node.tabsItems.length);
                    rebuildTabs(surfaceId, operation.nodeId, node);
                    return true;
                }
                if (!(view instanceof TextView)) return false;
                node.pickerSelected = clampPickerIndex((int) Math.round(operation.numberValue),
                        node.pickerOptions.length);
                updatePickerText((TextView) view, node);
                return true;
            case "mode":
                return operation.valueKind == MountOperation.VALUE_STRING &&
                        "text".equals(operation.stringValue) && view instanceof TextView;
            case "enabled":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN) return false;
                view.setEnabled(operation.booleanValue);
                return true;
            case "backgroundColor":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                node.backgroundColor = Color.parseColor(operation.stringValue);
                applyBackground(node);
                if (node.tabsView != null) applyTabsSelection(node);
                return true;
            case "color":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                if (node.tabsView != null) {
                    node.tabsTextColor = Color.parseColor(operation.stringValue);
                    applyTabsSelection(node);
                    return true;
                }
                if (!(view instanceof TextView)) return false;
                ((TextView) view).setTextColor(Color.parseColor(operation.stringValue));
                return true;
            case "borderRadius":
                if (operation.valueKind != MountOperation.VALUE_NUMBER) return false;
                node.borderRadius = logical(operation.numberValue);
                applyBackground(node);
                if (node.tabsView != null) applyTabsSelection(node);
                return true;
            case "fontSize":
                if (operation.valueKind != MountOperation.VALUE_NUMBER ||
                        !(view instanceof TextView)) return false;
                ((TextView) view).setTextSize((float) operation.numberValue);
                return true;
            case "textAlign":
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                        !(view instanceof TextView)) return false;
                int horizontal = "center".equals(operation.stringValue)
                        ? Gravity.CENTER_HORIZONTAL
                        : "right".equals(operation.stringValue)
                                ? Gravity.END : Gravity.START;
                ((TextView) view).setGravity(horizontal | Gravity.CENTER_VERTICAL);
                return true;
            default:
                return false;
        }
    }

    private boolean setLayout(String surfaceId, MountOperation operation) {
        NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
        if (node == null || operation.width < 0 || operation.height < 0) {
            return false;
        }
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                logical(operation.width), logical(operation.height));
        params.leftMargin = logical(operation.x);
        params.topMargin = logical(operation.y);
        node.view.setLayoutParams(params);
        if (node.tabsView != null) {
            Log.i(TAG, "android.tabs.layout surface=" + surfaceId +
                    " node=" + operation.nodeId +
                    " x=" + operation.x + " y=" + operation.y +
                    " width=" + operation.width + " height=" + operation.height);
        }
        return true;
    }

    private boolean moveNode(String surfaceId, MountOperation operation) {
        NodeRecord child = nodes.get(key(surfaceId, operation.nodeId));
        NodeRecord parent = nodes.get(key(surfaceId, operation.parentNodeId));
        if (child == null || parent == null || !(parent.view instanceof ViewGroup)) {
            return false;
        }
        ViewGroup parentGroup = (ViewGroup) parent.view;
        if (isDescendant(parentGroup, child.view)) {
            return false;
        }
        ViewGroup current = (ViewGroup) child.view.getParent();
        if (current != null) {
            current.removeView(child.view);
        }
        int index = Math.max(0, Math.min(operation.index, parentGroup.getChildCount()));
        parentGroup.addView(child.view, index);
        if (child.tabsView != null) {
            Log.i(TAG, "android.tabs.mount surface=" + surfaceId +
                    " node=" + operation.nodeId +
                    " parent=" + operation.parentNodeId +
                    " index=" + index +
                    " parentClass=" + parent.view.getClass().getSimpleName());
        }
        return true;
    }

    private boolean removeNode(String surfaceId, String nodeId) {
        NodeRecord node = nodes.get(key(surfaceId, nodeId));
        if (node == null) {
            return false;
        }
        removeNodeRecursive(surfaceId, node.view);
        return true;
    }

    private void removeNodeRecursive(String surfaceId, View view) {
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            while (group.getChildCount() > 0) {
                removeNodeRecursive(surfaceId, group.getChildAt(0));
            }
        }
        Object tag = view.getTag();
        NodeRecord record = tag instanceof String
                ? nodes.get(key(surfaceId, (String) tag)) : null;
        if (tag instanceof String) {
            nodes.remove(key(surfaceId, (String) tag));
        }
        ViewGroup parent = (ViewGroup) view.getParent();
        if (parent != null) {
            parent.removeView(view);
        }
        view.setOnClickListener(null);
        view.setOnFocusChangeListener(null);
        if (view instanceof Switch) ((Switch) view).setOnCheckedChangeListener(null);
        if (view instanceof ScrollView) {
            ScrollView scroll = (ScrollView) view;
            if (record != null && record.scrollEndRunnable != null) {
                scroll.removeCallbacks(record.scrollEndRunnable);
            }
            scroll.setOnScrollChangeListener(null);
        }
        if (record != null && record.videoView != null) {
            stopVideoTimeUpdates(surfaceId, (String) view.getTag(), record.videoView);
            record.videoView.setOnPreparedListener(null);
            record.videoView.setOnCompletionListener(null);
            record.videoView.setOnErrorListener(null);
            record.videoView.setOnClickListener(null);
            record.videoView.setMediaController(null);
            record.videoController = null;
            record.videoView.stopPlayback();
            record.videoPlayer = null;
        }
    }

    private void clearSurface(String surfaceId) {
        FrameLayout surface = surfaces.get(surfaceId);
        if (surface == null) return;
        while (surface.getChildCount() > 0) {
            removeNodeRecursive(surfaceId, surface.getChildAt(0));
        }
        nodes.entrySet().removeIf(entry -> entry.getValue().surfaceId.equals(surfaceId));
    }

    private void removeSurface(String surfaceId) {
        FrameLayout surface = surfaces.remove(surfaceId);
        if (surface == null) return;
        clearSurfaceContents(surfaceId, surface);
        appRoot.removeView(surface);
    }

    private void clearSurfaceContents(String surfaceId, FrameLayout surface) {
        while (surface.getChildCount() > 0) {
            removeNodeRecursive(surfaceId, surface.getChildAt(0));
        }
        nodes.entrySet().removeIf(entry -> entry.getValue().surfaceId.equals(surfaceId));
    }

    private void applyBackground(NodeRecord node) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(node.backgroundColor);
        drawable.setCornerRadius(node.tabsView == null
                ? node.borderRadius : Math.max(node.borderRadius, logical(20)));
        node.view.setBackground(drawable);
    }

    private void updateSlider(SeekBar slider, NodeRecord node, double min,
                              double max, double step, Double value) {
        if (!Double.isFinite(min) || !Double.isFinite(max) || !Double.isFinite(step) ||
                step <= 0 || max < min) return;
        int count = Math.max(1, (int) Math.round((max - min) / step));
        slider.setMax(count);
        if (value != null) {
            suppress(node.surfaceId, (String) node.view.getTag(), true);
            try {
                slider.setProgress(clampSliderProgress(value, min, max, step, count));
            } finally {
                suppress(node.surfaceId, (String) node.view.getTag(), false);
            }
        }
    }

    private int clampSliderProgress(double value, double min, double max,
                                    double step, int count) {
        int progress = (int) Math.round((value - min) / step);
        return Math.max(0, Math.min(count, progress));
    }

    private double sliderValue(NodeRecord node, int progress) {
        double value = node.sliderMin + progress * node.sliderStep;
        return Math.min(node.sliderMax, Math.max(node.sliderMin, value));
    }

    private int clampPickerIndex(int index, int size) {
        if (size <= 0) return 0;
        return Math.max(0, Math.min(size - 1, index));
    }

    private int clampTabsIndex(int index, int size) {
        if (size <= 0) return 0;
        return Math.max(0, Math.min(size - 1, index));
    }

    private void rebuildTabs(String surfaceId, String nodeId, NodeRecord node) {
        if (node.tabsView == null) return;
        node.tabsView.removeAllViews();
        for (int index = 0; index < node.tabsItems.length; index++) {
            final int tabIndex = index;
            TextView tab = new TextView(appRoot.getContext());
            tab.setGravity(Gravity.CENTER);
            tab.setText(node.tabsItems[index]);
            tab.setTextSize(14);
            tab.setTextColor(node.tabsTextColor);
            tab.setOnClickListener(ignored -> {
                NodeRecord current = nodes.get(key(surfaceId, nodeId));
                if (current == null || current.tabsItems.length == 0) return;
                current.tabsSelected = clampTabsIndex(tabIndex, current.tabsItems.length);
                applyTabsSelection(current);
                EventPayload payload = new EventPayload();
                payload.numberName = "index";
                payload.numberValue = current.tabsSelected;
                payload.hasNumber = true;
                payload.stringValue = current.tabsItems[current.tabsSelected];
                Log.i(TAG, "android.input.tabs.change surface=" + surfaceId +
                        " node=" + nodeId + " index=" + current.tabsSelected +
                        " value=" + payload.stringValue);
                eventSink.onEvent(surfaceId, nodeId, "change", payload,
                        android.os.SystemClock.elapsedRealtimeNanos());
            });
            node.tabsView.addView(tab, new LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        }
        applyTabsSelection(node);
    }

    private void applyTabsSelection(NodeRecord node) {
        if (node.tabsView == null) return;
        node.tabsSelected = clampTabsIndex(node.tabsSelected, node.tabsItems.length);
        for (int index = 0; index < node.tabsView.getChildCount(); index++) {
            View child = node.tabsView.getChildAt(index);
            if (!(child instanceof TextView)) continue;
            TextView tab = (TextView) child;
            tab.setTextColor(index == node.tabsSelected
                    ? node.backgroundColor : node.tabsTextColor);
            tab.setAlpha(index == node.tabsSelected ? 1f : 0.65f);
            tab.setTypeface(null, index == node.tabsSelected
                    ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
            if (index == node.tabsSelected) {
                GradientDrawable selectedBackground = new GradientDrawable();
                selectedBackground.setColor(Color.WHITE);
                selectedBackground.setCornerRadius(Math.max(node.borderRadius, logical(20)));
                tab.setBackground(selectedBackground);
            } else {
                tab.setBackground(null);
            }
        }
    }

    private void updatePickerText(TextView picker, NodeRecord node) {
        if (node.pickerOptions.length == 0) {
            picker.setText("");
        } else {
            node.pickerSelected = clampPickerIndex(node.pickerSelected,
                    node.pickerOptions.length);
            picker.setText(node.pickerOptions[node.pickerSelected]);
        }
    }

    private int scrollRange(ScrollView scroll) {
        if (scroll.getChildCount() == 0) return 0;
        View child = scroll.getChildAt(0);
        return Math.max(0, child.getHeight() - scroll.getHeight());
    }

    private void emitScroll(String surfaceId, String nodeId, ScrollView scroll,
                            NodeRecord node, String eventType) {
        EventPayload payload = new EventPayload();
        payload.scrollOffset = scroll.getScrollY() / density;
        payload.contentSize = scroll.getChildCount() == 0 ? 0
                : scroll.getChildAt(0).getHeight() / density;
        payload.viewportSize = scroll.getHeight() / density;
        payload.hasScrollMetrics = true;
        Log.i(TAG, "android.input." + eventType + " surface=" + surfaceId +
                " node=" + nodeId + " scrollOffset=" + payload.scrollOffset +
                " contentSize=" + payload.contentSize +
                " viewportSize=" + payload.viewportSize);
        eventSink.onEvent(surfaceId, nodeId, eventType, payload,
                android.os.SystemClock.elapsedRealtimeNanos());
    }

    private void emitVideoEvent(String surfaceId, String nodeId, String eventType,
                                Double currentTime) {
        EventPayload payload = new EventPayload();
        if (currentTime != null) {
            payload.numberName = "currentTime";
            payload.numberValue = currentTime;
            payload.hasNumber = true;
        }
        eventSink.onEvent(surfaceId, nodeId, eventType, payload,
                android.os.SystemClock.elapsedRealtimeNanos());
    }

    private void scheduleVideoTimeUpdates(String surfaceId, String nodeId,
                                           VideoView video, NodeRecord node) {
        if (node.videoProgressRunnable != null) return;
        node.videoProgressRunnable = () -> {
            if (!nodes.containsKey(key(surfaceId, nodeId)) || node.videoView != video) return;
            if (video.isPlaying()) {
                emitVideoEvent(surfaceId, nodeId, "timeupdate",
                        video.getCurrentPosition() / 1000.0);
                video.postDelayed(node.videoProgressRunnable, 500L);
            }
        };
        video.postDelayed(node.videoProgressRunnable, 500L);
    }

    private void stopVideoTimeUpdates(String surfaceId, String nodeId, VideoView video) {
        NodeRecord node = nodes.get(key(surfaceId, nodeId));
        if (node != null && node.videoProgressRunnable != null) {
            video.removeCallbacks(node.videoProgressRunnable);
            node.videoProgressRunnable = null;
        }
    }

    private void showPicker(String surfaceId, String nodeId, TextView picker) {
        NodeRecord node = nodes.get(key(surfaceId, nodeId));
        if (node == null || node.pickerOptions.length == 0) return;
        final int[] pending = {node.pickerSelected};
        new AlertDialog.Builder(appRoot.getContext())
                .setTitle("选择")
                .setSingleChoiceItems(node.pickerOptions, node.pickerSelected,
                        (dialog, which) -> pending[0] = which)
                .setNegativeButton("取消", null)
                .setPositiveButton("确认", (dialog, which) -> {
                    node.pickerSelected = clampPickerIndex(pending[0],
                            node.pickerOptions.length);
                    updatePickerText(picker, node);
                    EventPayload payload = new EventPayload();
                    payload.numberValue = node.pickerSelected;
                    payload.numberName = "selected";
                    payload.hasNumber = true;
                    payload.stringValue = node.pickerOptions[node.pickerSelected];
                    Log.i(TAG, "android.input.picker.change surface=" + surfaceId +
                            " node=" + nodeId + " selected=" + node.pickerSelected +
                            " value=" + payload.stringValue);
                    eventSink.onEvent(surfaceId, nodeId, "change", payload,
                            android.os.SystemClock.elapsedRealtimeNanos());
                })
                .show();
    }

    private boolean loadImage(ImageView image, String member) {
        if (member == null || member.isEmpty() || rpkPath == null) return false;
        try (ZipFile packageFile = new ZipFile(rpkPath)) {
            java.util.zip.ZipEntry entry = packageFile.getEntry(member);
            if (entry == null) return false;
            try (InputStream input = packageFile.getInputStream(entry)) {
                android.graphics.Bitmap bitmap = android.graphics.BitmapFactory.decodeStream(input);
                if (bitmap == null) return false;
                image.setImageBitmap(bitmap);
                return true;
            }
        } catch (IOException failure) {
            Log.w(TAG, "android.image.load.failed member=" + member, failure);
            return false;
        }
    }

    private Uri videoUri(String source) {
        if (source == null || source.isEmpty()) return null;
        if (!source.startsWith("assets/")) return Uri.parse(source);
        File cached = materializedVideos.get(source);
        if (cached != null && cached.isFile()) return Uri.fromFile(cached);
        if (rpkPath == null) return null;
        String safeName = source.replaceAll("[^A-Za-z0-9._-]", "_");
        File output = new File(appRoot.getContext().getCacheDir(),
                "quickapp-kit-video-" + safeName);
        try (ZipFile packageFile = new ZipFile(rpkPath)) {
            java.util.zip.ZipEntry entry = packageFile.getEntry(source);
            if (entry == null) return null;
            try (InputStream input = packageFile.getInputStream(entry);
                 FileOutputStream stream = new FileOutputStream(output, false)) {
                byte[] buffer = new byte[16 * 1024];
                int read;
                while ((read = input.read(buffer)) >= 0) {
                    if (read > 0) stream.write(buffer, 0, read);
                }
                stream.getFD().sync();
            }
            materializedVideos.put(source, output);
            Log.i(TAG, "android.video.resource.materialized member=" + source +
                    " bytes=" + output.length());
            return Uri.fromFile(output);
        } catch (IOException failure) {
            Log.w(TAG, "android.video.resource.failed member=" + source, failure);
            if (output.exists()) output.delete();
            return null;
        }
    }

    private boolean isDescendant(View candidateParent, View candidateChild) {
        View current = candidateParent;
        while (current != null) {
            if (current == candidateChild) return true;
            if (!(current.getParent() instanceof View)) return false;
            current = (View) current.getParent();
        }
        return false;
    }

    private int logical(double value) {
        return Math.max(0, Math.round((float) value * density));
    }

    private String key(String surfaceId, String nodeId) {
        return surfaceId + '\u0000' + nodeId;
    }

    private void suppress(String surfaceId, String nodeId, boolean value) {
        NodeRecord node = nodes.get(key(surfaceId, nodeId));
        if (node != null) node.suppressEvents = value;
    }

    private boolean isSuppressed(String surfaceId, String nodeId) {
        NodeRecord node = nodes.get(key(surfaceId, nodeId));
        return node != null && node.suppressEvents;
    }
}
