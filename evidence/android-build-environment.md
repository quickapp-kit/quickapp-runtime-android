# Android Build Environment Verification

Date: 2026-08-15

## Environment

| Component | Version |
|-----------|---------|
| Gradle | 9.5.0 (wrapper) |
| Android Gradle Plugin | 9.3.1 |
| JDK | JetBrains JBR 21.0.10 (Android Studio Embedded) |
| Android SDK | Platform 36 (revision 2) |
| NDK | 28.2.13676358 |
| CMake | 4.1.2 (SDK-installed) |
| OS | macOS 26.5.1 aarch64 |

## Build Command

```bash
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
./gradlew :app:assembleDebug --no-daemon --no-configuration-cache
```

## Result

- **Status**: BUILD SUCCESSFUL
- **Duration**: ~45s
- **APK path**: `app/build/outputs/apk/debug/app-debug.apk`
- **Tasks executed**: 40 actionable (18 executed, 22 up-to-date)

## Issues Fixed During Verification

1. **Gradle 9.5.0 cache corruption** — previous download was incomplete (`.zip.part`). Moved broken cache to `/private/tmp/`, re-downloaded successfully.

2. **`srcDir(Provider)` not allowed** (AGP 9.3.1) — `app/build.gradle.kts` line 35 used `layout.buildDirectory.dir(...)` as a Provider argument to `srcDir()`. Changed to string path.

3. **`<uses-sdk>` in AndroidManifest.xml** — AGP 9.0+ no longer allows SDK version attributes in manifest. Removed `<uses-sdk>` tag (minSdk/targetSdk already declared in build.gradle.kts).

4. **`package` attribute in manifest** — AGP 9.x ignores it; namespace is set via `build.gradle.kts`. Removed to eliminate warning.

5. **CMake version mismatch** — `app/build.gradle.kts` specified CMake 3.22.1 but root `CMakeLists.txt` requires `cmake_minimum_required(VERSION 3.24)`. Updated to 4.1.2 (already installed in SDK).

6. **`android.useAndroidX=false`** — deprecated in AGP 9.x (default is true). Changed to `true` in `gradle.properties`.

## Scope Confirmation

- No modifications to Core, JS, Toolkit, LVGL, Examples, or public Contracts.
- All changes are within the Android project (`app/build.gradle.kts`, `gradle.properties`, `AndroidManifest.xml`).

## Remaining Issues

- None. Build completes successfully. One deprecation warning remains (`srcDir` API is deprecated but functional; migration to Variant API `addStaticDirectories` is non-blocking).
