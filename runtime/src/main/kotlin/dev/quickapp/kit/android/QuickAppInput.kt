package dev.quickapp.kit.android

class QuickAppInput(
    @JvmField val action: Int,
    @JvmField val x: Float,
    @JvmField val y: Float,
    @JvmField val timestampNs: Long
) {
    init {
        require(!(action < TOUCH_DOWN || action > TOUCH_CANCEL)) {
            "Unsupported touch action"
        }
    }

    companion object {
        const val TOUCH_DOWN = 0
        const val TOUCH_UP = 1
        const val TOUCH_MOVE = 2
        const val TOUCH_CANCEL = 3

        @JvmStatic
        fun touch(action: Int, x: Float, y: Float, timestampNs: Long): QuickAppInput =
            QuickAppInput(action, x, y, timestampNs)
    }
}
