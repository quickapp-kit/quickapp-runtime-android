package dev.quickapp.kit.android

class MountTransaction(
    @JvmField val surfaceId: String?,
    @JvmField val revision: Long,
    @JvmField val mountAttemptId: String?,
    @JvmField val sourceId: String?,
    @JvmField val full: Boolean,
    @JvmField val operations: Array<MountOperation?>?
)
