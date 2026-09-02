package dev.quickapp.kit.android;

public final class QuickAppInput {
    public static final int TOUCH_DOWN = 0;
    public static final int TOUCH_UP = 1;
    public static final int TOUCH_MOVE = 2;
    public static final int TOUCH_CANCEL = 3;

    public final int action;
    public final float x;
    public final float y;
    public final long timestampNs;

    public QuickAppInput(int action, float x, float y, long timestampNs) {
        if (action < TOUCH_DOWN || action > TOUCH_CANCEL) {
            throw new IllegalArgumentException("Unsupported touch action");
        }
        this.action = action;
        this.x = x;
        this.y = y;
        this.timestampNs = timestampNs;
    }

    public static QuickAppInput touch(int action, float x, float y, long timestampNs) {
        return new QuickAppInput(action, x, y, timestampNs);
    }
}
