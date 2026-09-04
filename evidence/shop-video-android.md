# Shop Video Android Evidence

## Conclusion

The Android video path works with the shop RPK currently present on disk. The
platform materializes the packaged video, configures `VideoView`, receives
`prepared`, and supports click-to-play/pause when `controls=false`. Video
events return through the existing Runtime event path. No Core, JS, Toolkit,
RPK, Contract, Router, or Runtime Tree was changed.

## Input And Integrity

- Source RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/shop/dist/shop.rpk`
- Requested delegated SHA-256: `f4a21abc7480a1d96134c4f7fdaf5a4f45ae745ad26457b689ebf7b9fd67d719`
- Actual source SHA-256 used: `864ad477c8c4399fa191a1a98506f5c09e5127140772596a8baedb6a54d41b0b`
- Generated Host RPK SHA-256: `864ad477c8c4399fa191a1a98506f5c09e5127140772596a8baedb6a54d41b0b`
- APK: `app/build/outputs/apk/debug/quickapp-host-debug.apk`
- APK SHA-256: `56e7c1fd382fbfdd62d2c2e1b1a8d4b4e4f33a8c1c40e091a608df9655ad8f54`
- Packaged video: `assets/videos/seed-demo.mp4`
- Packaged video SHA-256: `1456ac6014de2e2fa2aa5412002648b872e428bc7ddce1c7d532b4ffdfe0fcbb`
- Materialized cache video SHA-256: `1456ac6014de2e2fa2aa5412002648b872e428bc7ddce1c7d532b4ffdfe0fcbb`

The requested `f4a21...` input was not present at verification time. The
verification therefore covers the current on-disk RPK (`864ad...`), not the
older delegated hash.

## Build And Run

```sh
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android
./gradlew clean :quickapp-host:assembleDebug --no-daemon --no-configuration-cache --console=plain
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am force-stop dev.quickapp.kit.host
adb shell am start -n dev.quickapp.kit.host/.MainActivity --es quickapp.rpk shop.rpk
```

## Runtime Evidence

- RPK verification and module loading succeeded.
- Video node: `node=node:181`.
- `android.video.resource.materialized member=assets/videos/seed-demo.mp4 bytes=1358537`.
- `android.video.set_uri node=node:181 uri=file:///data/user/0/dev.quickapp.kit.host/cache/quickapp-kit-video-assets_videos_seed-demo.mp4`.
- `android.video.prepared surface=srf:1 node=node:181`.
- First click emitted `android.event.start.received` and `android.event.start.dispatched=1`.
- Second click emitted `android.event.pause.received` and `android.event.pause.dispatched=1`.
- Existing `onVideoPrepared`, `onVideoStart`, and `onVideoPause` handlers were bound and executed.
- Existing invalid-media fallback reports `android.video.error` and dispatches `error`; no silent failure path was added.

## Teardown

```text
android.runtime.destroy.begin surfaces=1 nodes=36
android.runtime.destroy.end surfaces=0 nodes=0
android.runtime.stopped surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0
```

## Changed Android File

- `runtime/src/main/java/dev/quickapp/kit/android/RuntimeSurfaceHost.java`

The platform change adds one pending play flag for pre-`prepared` clicks,
forwards poster clicks to the real `VideoView`, and logs the resolved URI.

## Remaining Issue

The delegated SHA-256 (`f4a21...`) does not match the current source RPK
(`864ad...`). No code-side workaround was used; rerun exact-hash validation
after the intended RPK is restored.
