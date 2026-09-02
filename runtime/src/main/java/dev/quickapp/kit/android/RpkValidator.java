package dev.quickapp.kit.android;

import java.io.File;
import java.io.IOException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class RpkValidator {
    private static final long MAX_PACKAGE_BYTES = 64L * 1024L * 1024L;
    private static final long MAX_ENTRY_BYTES = 32L * 1024L * 1024L;

    private RpkValidator() {}

    static QuickAppResult validate(File rpk) {
        if (rpk == null || !rpk.isFile() || !rpk.canRead()) {
            return QuickAppResult.failed("RPK_NOT_READABLE", "RPK is not a readable file");
        }
        if (rpk.length() <= 0 || rpk.length() > MAX_PACKAGE_BYTES) {
            return QuickAppResult.failed("RPK_SIZE_INVALID", "RPK size is outside the allowed limit");
        }
        long uncompressedBytes = 0;
        try (ZipFile packageFile = new ZipFile(rpk)) {
            if (packageFile.size() == 0) {
                return QuickAppResult.failed("RPK_EMPTY", "RPK has no entries");
            }
            for (var entries = packageFile.entries(); entries.hasMoreElements();) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();
                if (!safePath(name) || nativeCode(name)) {
                    return QuickAppResult.failed("RPK_MEMBER_REJECTED", "RPK contains a rejected member");
                }
                long size = entry.getSize();
                if (size > MAX_ENTRY_BYTES) {
                    return QuickAppResult.failed("RPK_ENTRY_TOO_LARGE", "RPK member exceeds the allowed limit");
                }
                if (size >= 0) {
                    uncompressedBytes += size;
                    if (uncompressedBytes > MAX_PACKAGE_BYTES * 4) {
                        return QuickAppResult.failed("RPK_UNCOMPRESSED_TOO_LARGE", "RPK expands beyond the allowed limit");
                    }
                }
            }
        } catch (IOException failure) {
            return QuickAppResult.failed("RPK_INVALID", "RPK is not a readable zip package");
        }
        return QuickAppResult.completed();
    }

    private static boolean safePath(String name) {
        if (name == null || name.isEmpty() || name.startsWith("/") || name.startsWith("\\") || name.contains("\\")) {
            return false;
        }
        for (String segment : name.split("/")) {
            if (segment.isEmpty() || ".".equals(segment) || "..".equals(segment)) return false;
        }
        return true;
    }

    private static boolean nativeCode(String name) {
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        return lower.endsWith(".so") || lower.endsWith(".dll") || lower.endsWith(".dylib") ||
                lower.endsWith(".aar") || lower.endsWith(".apk");
    }
}
