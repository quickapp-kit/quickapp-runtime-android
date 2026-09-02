package dev.quickapp.kit.android;

public final class MountOperation {
    public static final int CREATE = 0;
    public static final int SET_PROP = 1;
    public static final int SET_LAYOUT = 2;
    public static final int INSERT = 3;
    public static final int MOVE = 4;
    public static final int REMOVE = 5;

    public static final int VALUE_NONE = 0;
    public static final int VALUE_BOOLEAN = 1;
    public static final int VALUE_NUMBER = 2;
    public static final int VALUE_STRING = 3;

    public final int kind;
    public final String nodeId;
    public final String parentNodeId;
    public final int componentType;
    public final String propertyName;
    public final int valueKind;
    public final boolean booleanValue;
    public final double numberValue;
    public final String stringValue;
    public final float x;
    public final float y;
    public final float width;
    public final float height;
    public final int index;

    public MountOperation(
            int kind,
            String nodeId,
            String parentNodeId,
            int componentType,
            String propertyName,
            int valueKind,
            boolean booleanValue,
            double numberValue,
            String stringValue,
            float x,
            float y,
            float width,
            float height,
            int index) {
        this.kind = kind;
        this.nodeId = nodeId;
        this.parentNodeId = parentNodeId;
        this.componentType = componentType;
        this.propertyName = propertyName;
        this.valueKind = valueKind;
        this.booleanValue = booleanValue;
        this.numberValue = numberValue;
        this.stringValue = stringValue;
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
        this.index = index;
    }
}
