package dev.quickapp.kit.android

class QuickAppResult private constructor(
    @JvmField val status: Status,
    @JvmField val errorCode: String?,
    @JvmField val message: String?
) {
    enum class Status {
        ACCEPTED,
        COMPLETED,
        FAILED,
        UNSUPPORTED,
        CANCELLED
    }

    val isAccepted: Boolean
        get() = status == Status.ACCEPTED

    val isSuccess: Boolean
        get() = status == Status.ACCEPTED || status == Status.COMPLETED

    companion object {
        @JvmStatic
        fun accepted(): QuickAppResult = QuickAppResult(Status.ACCEPTED, null, null)

        @JvmStatic
        fun completed(): QuickAppResult = QuickAppResult(Status.COMPLETED, null, null)

        @JvmStatic
        fun failed(code: String?, message: String?): QuickAppResult =
            QuickAppResult(Status.FAILED, code, message)

        @JvmStatic
        fun unsupported(message: String?): QuickAppResult =
            QuickAppResult(Status.UNSUPPORTED, "UNSUPPORTED", message)

        @JvmStatic
        fun destroyed(): QuickAppResult =
            failed("RUNTIME_DESTROYED", "Runtime has been destroyed")
    }
}
