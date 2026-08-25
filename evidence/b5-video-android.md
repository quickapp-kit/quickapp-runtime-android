# B5 Video: Android Evidence

## Conclusion

Android B5 Video is implemented with the platform `VideoView` and
`MediaController`. The real `media-001.rpk` loads through the existing
RPK -> JS -> Core -> Android Runtime path, mounts the Video host component,
shows its poster, and delivers the invalid-source error through the existing
Core Event Router to the JS `onError` handler. No Core, JS Framework, Toolkit,
Contract, or example source was changed.

## Scope

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/media-001/dist/media-001.rpk`
- RPK SHA-256: `439009523904f8335f96902e642e6d2150379dacdc28d3bceb690923ea0ba0df`
- APK: `app/build/outputs/apk/debug/app-debug.apk`
- Supported Android Video properties: `src`, `poster`, `controls`, `muted`, `autoplay`
- Supported events: `prepared`, `start`, `pause`, `finish`, `error`, `timeupdate`
- Deliberately excluded: live streaming, seek, screenshots, complex fullscreen,
  and custom controls.

`media-001.rpk` uses `https://example.invalid/quickapp-kit/demo.mp4`, so a
successful playback event cannot be claimed. The deterministic acceptance
condition is a visible poster plus a non-crashing platform error routed to JS.
The case contains one Home page and no route interaction.

## Validation

Build and install:

```sh
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android
./gradlew :app:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk media-001.rpk
```

Observed:

- `android.stage=rpk.verified`
- `android.platform.mount.result surface=srf:1 ok=true`
- Video properties applied: `autoplay`, `controls`, `muted`, `poster`, `src`
- `android.video.error surface=srf:1 node=node:3 what=1 extra=-2147483648`
- `android.event.error.received surface=srf:1 node=node:3`
- `android.event.error.dispatched=1`
- `android.event.handler_execute surface=srf:1 handler=hdl:5 dispatched=1`
- No `AndroidRuntime` crash after the media error
- Final screenshot shows the Home page and poster fallback:
  `evidence/media-001-android-final.png`

Logs:

- `evidence/media-001-android.log`
- `evidence/media-001-android-teardown.log`

The Android Activity teardown path synchronously closes platform surfaces and
VideoView resources before invoking the existing Native Runtime destroy:
`android.runtime.destroy.begin` -> `RuntimeSurfaceHost.close()` ->
`android.runtime.destroy.end`. The emulator returned to the launcher without
an Android crash. Because the process exits during Activity teardown, the
asynchronous native stop callback is not used as the resource proof; the
platform cleanup path is covered by the Android implementation and logs are
retained in `media-001-android-teardown.log`.

## Boundary

The Android adapter owns native VideoView creation, poster fallback, media
callbacks, MediaController attachment, and media cleanup. Core continues to
own Runtime Tree, event routing, lifecycle, and transaction semantics. The
adapter creates no second tree, route stack, or business state.
