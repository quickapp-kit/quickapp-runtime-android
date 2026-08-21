package dev.quickapp.kit.android;

public final class MountTransaction {
    public final String surfaceId;
    public final long revision;
    public final String mountAttemptId;
    public final String sourceId;
    public final boolean full;
    public final MountOperation[] operations;

    public MountTransaction(
            String surfaceId,
            long revision,
            String mountAttemptId,
            String sourceId,
            boolean full,
            MountOperation[] operations) {
        this.surfaceId = surfaceId;
        this.revision = revision;
        this.mountAttemptId = mountAttemptId;
        this.sourceId = sourceId;
        this.full = full;
        this.operations = operations;
    }
}
