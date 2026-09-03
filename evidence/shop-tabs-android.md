# Shop Tabs Android Verification

## Conclusion

`shop.rpk` Tabs are visible and interactive after the Host waits for the Runtime surface to be measured before attachment. Core, JS, Toolkit, RPK, Router, Runtime Tree, and public contracts were not changed.

## Root Cause

The Host called `attachSurface()` before `runtimeSurface` had a measured size. The SDK fallback used full display dimensions, including the Host header, so Core correctly laid Tabs at `y=894`, outside the actual Runtime viewport. Attaching on the next UI traversal supplies the real container size and places Tabs at `y=750`.

## Input

- RPK: `quickapp-examples/showcases/shop/dist/shop.rpk`
- SHA-256: `f4a21abc7480a1d96134c4f7fdaf5a4f45ae745ad26457b689ebf7b9fd67d719`

## Build And Install

```text
./gradlew clean :quickapp-host:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
```

Cold build and installation succeeded.

## Verification

- Tabs mount: `node=node:9 parent=node:1 index=2 parentClass=FrameLayout`
- Corrected layout: `x=12 y=750 width=403 height=48`
- Android accessibility tree exposes all four clickable items: `首页`, `种草`, `购物车`, `我的`.
- Sequence `0 -> 1 -> 2 -> 3 -> 0` emitted typed `change` events with numeric `index` and string `value`.
- Every change executed JS handler `hdl:13`, submitted a new `RenderTransaction`, and completed platform Mount successfully.
- Exit completed with `surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0`.

## Changed Files

- `app/src/main/java/dev/quickapp/kit/host/MainActivity.java`
- `runtime/src/main/java/dev/quickapp/kit/android/RuntimeSurfaceHost.java`

## Remaining Issues

None for Android Tabs visibility or interaction in `shop.rpk`.
