package dev.quickapp.kit.android;

public final class QuickAppResult {
    public enum Status {
        ACCEPTED,
        COMPLETED,
        FAILED,
        UNSUPPORTED,
        CANCELLED
    }

    public final Status status;
    public final String errorCode;
    public final String message;

    private QuickAppResult(Status status, String errorCode, String message) {
        this.status = status;
        this.errorCode = errorCode;
        this.message = message;
    }

    public static QuickAppResult accepted() {
        return new QuickAppResult(Status.ACCEPTED, null, null);
    }

    public static QuickAppResult completed() {
        return new QuickAppResult(Status.COMPLETED, null, null);
    }

    public static QuickAppResult failed(String code, String message) {
        return new QuickAppResult(Status.FAILED, code, message);
    }

    public static QuickAppResult unsupported(String message) {
        return new QuickAppResult(Status.UNSUPPORTED, "UNSUPPORTED", message);
    }

    public static QuickAppResult destroyed() {
        return failed("RUNTIME_DESTROYED", "Runtime has been destroyed");
    }

    public boolean isAccepted() {
        return status == Status.ACCEPTED;
    }

    public boolean isSuccess() {
        return status == Status.ACCEPTED || status == Status.COMPLETED;
    }
}
