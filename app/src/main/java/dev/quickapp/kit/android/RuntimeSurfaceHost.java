package dev.quickapp.kit.android;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.util.HashMap;
import java.util.Map;

final class RuntimeSurfaceHost {
    private static final int COMPONENT_VIEW = 0;
    private static final int COMPONENT_TEXT = 1;
    private static final int COMPONENT_BUTTON = 2;

    interface ClickSink {
        void onClick(String surfaceId, String nodeId, long timestampNs);
    }

    private static final class NodeRecord {
        final String surfaceId;
        final View view;
        int backgroundColor = Color.TRANSPARENT;
        float borderRadius;

        NodeRecord(String surfaceId, View view) {
            this.surfaceId = surfaceId;
            this.view = view;
        }
    }

    private final FrameLayout appRoot;
    private final ClickSink clickSink;
    private final float density;
    private final Map<String, FrameLayout> surfaces = new HashMap<>();
    private final Map<String, NodeRecord> nodes = new HashMap<>();

    RuntimeSurfaceHost(FrameLayout appRoot, ClickSink clickSink) {
        this.appRoot = appRoot;
        this.clickSink = clickSink;
        this.density = appRoot.getResources().getDisplayMetrics().density;
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
        removeSurface(sourceSurfaceId);
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
                    return false;
                }
            }
            return true;
        } catch (RuntimeException failure) {
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
            button.setOnClickListener(ignored -> clickSink.onClick(
                    surfaceId, operation.nodeId, android.os.SystemClock.elapsedRealtimeNanos()));
            view = button;
        } else if (operation.componentType == COMPONENT_VIEW) {
            view = new FrameLayout(appRoot.getContext());
        } else {
            return false;
        }
        view.setTag(operation.nodeId);
        surface.addView(view, new FrameLayout.LayoutParams(0, 0));
        nodes.put(key, new NodeRecord(surfaceId, view));
        return true;
    }

    private boolean setProperty(String surfaceId, MountOperation operation) {
        NodeRecord node = nodes.get(key(surfaceId, operation.nodeId));
        if (node == null || operation.propertyName == null) {
            return false;
        }
        View view = node.view;
        switch (operation.propertyName) {
            case "text":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                if (view instanceof TextView) {
                    ((TextView) view).setText(operation.stringValue);
                    return true;
                }
                return false;
            case "enabled":
                if (operation.valueKind != MountOperation.VALUE_BOOLEAN) return false;
                view.setEnabled(operation.booleanValue);
                return true;
            case "backgroundColor":
                if (operation.valueKind != MountOperation.VALUE_STRING) return false;
                node.backgroundColor = Color.parseColor(operation.stringValue);
                applyBackground(node);
                return true;
            case "color":
                if (operation.valueKind != MountOperation.VALUE_STRING ||
                        !(view instanceof TextView)) return false;
                ((TextView) view).setTextColor(Color.parseColor(operation.stringValue));
                return true;
            case "borderRadius":
                if (operation.valueKind != MountOperation.VALUE_NUMBER) return false;
                node.borderRadius = logical(operation.numberValue);
                applyBackground(node);
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
        if (tag instanceof String) {
            nodes.remove(key(surfaceId, (String) tag));
        }
        ViewGroup parent = (ViewGroup) view.getParent();
        if (parent != null) {
            parent.removeView(view);
        }
        view.setOnClickListener(null);
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
        drawable.setCornerRadius(node.borderRadius);
        node.view.setBackground(drawable);
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
}
