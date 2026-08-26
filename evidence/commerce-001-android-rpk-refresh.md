# commerce-001 Android RPK Refresh Revalidation

## Conclusion

The updated `commerce-001.rpk` was rebuilt into Android assets, installed, and
launched on `emulator-5554`. The updated package changes the Home layout and
now contains the public numeric `updateBinding` validator fix. Android verified:

- real RPK load and Home mount;
- Image/Text/Button and keyed product blocks;
- ordinary button state refresh with a successful RenderTransaction;
- detail `push -> back -> push -> back`.

Tabs remains unresolved: the native selected label changes and the event reaches
`onTabChange`, but no RenderTransaction is submitted afterward. Therefore the
bound `selectedTab` `if` content does not change. This is not bypassed with
Android state and is recorded as a JS reactive/event-to-render issue pending
public-layer diagnosis.

Teardown remains incomplete evidence: `destroy.begin surfaces=3 nodes=55` was
observed, but the process exited before `runtime.stopped surfaces=0 nodes=0`.
There was no Android crash.

## Input and build

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- Updated SHA-256: `fb0420bf561224518c2278dcf468701aa889ba15e60aa75fa68ba95ae0377b8f`
- Build: `./gradlew :app:assembleDebug --no-daemon`
- Install: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- Launch: `adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk commerce-001.rpk`

The Android-only diagnostic log added to `src/runtime_spine.cpp` reports the
Core submission result without changing transaction behavior.

## Evidence

- Home: `commerce-001-android-home-v2.png`
- Category attempt: `commerce-001-android-category-v3.png`
- Refresh: `commerce-001-android-refresh-v2.png`
- Detail first: `commerce-001-android-detail-v2.png`
- Home after first back: `commerce-001-android-home-after-back-v2.png`
- Detail repeated: `commerce-001-android-detail-v2-repeat.png`
- Final Home: `commerce-001-android-home-final-v2.png`
- Start log: `commerce-001-android-start-v2.log`
- Category log: `commerce-001-android-category-v3.log`
- Refresh log: `commerce-001-android-refresh-v2.log`
- Detail log: `commerce-001-android-detail-v2.log`
- Full route log: `commerce-001-android-full-v2.log`
- Teardown: `commerce-001-android-teardown-v2.log`

## Key observations

Refresh succeeds:

```text
android.native.mount surface=srf:1 operations=8
android.render.submit surface=srf:1 transaction=txn:srf:1-1 revision=1 ok=1 error=
android.mount.result.received surface=srf:1 revision=1 ... mounted=1
```

Tabs does not submit a transaction:

```text
android.input.tabs.change surface=srf:1 node=node:8 index=1 value=分类
android.event.change.dispatched=1
android.event.handler_execute surface=srf:1 handler=hdl:3 dispatched=1
```

No `android.render.submit` follows the Tabs handler. The native label changes,
but the page remains Home. This separates platform input delivery from the
public JS reactive render submission path.

Route re-entry succeeds:

```text
android.navigation.push ... source=srf:1 ... accepted=1
android.platform.mount.result surface=srf:2 ok=true
android.navigation.close ... source=srf:2 ... accepted=1
android.navigation.push ... source=srf:1 ... accepted=1
android.platform.mount.result surface=srf:3 ok=true
android.navigation.close ... source=srf:3 ... accepted=1
```

No Core, Toolkit, public Contract, Examples, or other platform was modified for
this revalidation. Android changes are limited to its platform/runtime project
and evidence.
