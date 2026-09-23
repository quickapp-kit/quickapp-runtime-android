package dev.quickapp.kit.android

class MountOperation(
    @JvmField val kind: Int,
    @JvmField val nodeId: String?,
    @JvmField val parentNodeId: String?,
    @JvmField val componentType: Int,
    @JvmField val propertyName: String?,
    @JvmField val valueKind: Int,
    @JvmField val booleanValue: Boolean,
    @JvmField val numberValue: Double,
    @JvmField val stringValue: String?,
    @JvmField val x: Float,
    @JvmField val y: Float,
    @JvmField val width: Float,
    @JvmField val height: Float,
    @JvmField val index: Int
) {
    companion object {
        const val CREATE = 0
        const val SET_PROP = 1
        const val SET_LAYOUT = 2
        const val INSERT = 3
        const val MOVE = 4
        const val REMOVE = 5

        const val VALUE_NONE = 0
        const val VALUE_BOOLEAN = 1
        const val VALUE_NUMBER = 2
        const val VALUE_STRING = 3
    }
}
