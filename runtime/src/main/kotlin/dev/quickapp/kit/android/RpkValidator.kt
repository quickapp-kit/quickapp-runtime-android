package dev.quickapp.kit.android

import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.zip.ZipFile

internal object RpkValidator {
    private const val MAX_PACKAGE_BYTES = 64L * 1024L * 1024L
    private const val MAX_ENTRY_BYTES = 32L * 1024L * 1024L

    fun validate(rpk: File?): QuickAppResult {
        if (rpk == null || !rpk.isFile || !rpk.canRead()) {
            return QuickAppResult.failed("RPK_NOT_READABLE", "RPK is not a readable file")
        }
        if (rpk.length() <= 0 || rpk.length() > MAX_PACKAGE_BYTES) {
            return QuickAppResult.failed("RPK_SIZE_INVALID", "RPK size is outside the allowed limit")
        }
        var uncompressedBytes = 0L
        try {
            ZipFile(rpk).use { packageFile ->
                if (packageFile.size() == 0) {
                    return QuickAppResult.failed("RPK_EMPTY", "RPK has no entries")
                }
                val entries = packageFile.entries()
                while (entries.hasMoreElements()) {
                    val entry = entries.nextElement()
                    val name = entry.name
                    if (!safePath(name) || nativeCode(name)) {
                        return QuickAppResult.failed(
                            "RPK_MEMBER_REJECTED", "RPK contains a rejected member")
                    }
                    val size = entry.size
                    if (size > MAX_ENTRY_BYTES) {
                        return QuickAppResult.failed(
                            "RPK_ENTRY_TOO_LARGE", "RPK member exceeds the allowed limit")
                    }
                    if (size >= 0) {
                        uncompressedBytes += size
                        if (uncompressedBytes > MAX_PACKAGE_BYTES * 4) {
                            return QuickAppResult.failed(
                                "RPK_UNCOMPRESSED_TOO_LARGE", "RPK expands beyond the allowed limit")
                        }
                    }
                }
            }
        } catch (failure: IOException) {
            return QuickAppResult.failed("RPK_INVALID", "RPK is not a readable zip package")
        }
        return QuickAppResult.completed()
    }

    private fun safePath(name: String?): Boolean {
        if (name.isNullOrEmpty() || name.startsWith("/") || name.startsWith("\\") ||
            name.contains("\\")
        ) {
            return false
        }
        for (segment in name.split("/")) {
            if (segment.isEmpty() || "." == segment || ".." == segment) return false
        }
        return true
    }

    private fun nativeCode(name: String): Boolean {
        val lower = name.lowercase(Locale.ROOT)
        return lower.endsWith(".so") || lower.endsWith(".dll") || lower.endsWith(".dylib") ||
            lower.endsWith(".aar") || lower.endsWith(".apk")
    }
}
