# commerce-001 Android JS ABI validation (2026-08-26)

## Conclusion

Android was rebuilt against the current workspace `quickapp-runtime-js`, the
latest APK was installed, and the real `commerce-001.rpk` was launched on
`emulator-5554`.

The numeric `updateBinding.value` path works for the ordinary refresh handler:
the handler submits `revision=1`, Android mounts the transaction, and the
runtime reports `mounted=1`. Tabs input is different: all four tab changes reach
the JS handler and change native selection, but none submits a
`RenderTransaction`, so the conditional page content remains Home.

The page-level route path is healthy. The in-page back button completed two
cycles: `srf:1 -> srf:2 -> srf:1`, then `srf:1 -> srf:3 -> srf:1`.

## Inputs and build

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- RPK SHA-256: `d0317e888354356a965c1eb7e8b07aa17fbe9aad99c447223b348607c5b780d4`
- Android asset SHA-256: `d0317e888354356a965c1eb7e8b07aa17fbe9aad99c447223b348607c5b780d4`
- APK SHA-256: `9e2600be195367259984be86525de659251355fb8ebbabf669550218acfb1fc9`
- Build: `./gradlew :app:assembleDebug --no-daemon`
- Install: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- Launch: `adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk commerce-001.rpk`

## Results

| Capability | Result | Evidence |
| --- | --- | --- |
| Latest RPK included in APK | PASS | asset SHA matches source |
| Home initial mount | PASS | `android.native.mount`, `mounted=1` |
| Tabs: 分类/购物车/我的/首页 input | PASS | `commerce-001-android-jsabi-tabs.log` |
| Tabs -> JS handler | PASS | `handler=hdl:3 dispatched=1` |
| Tabs -> numeric updateBinding | BLOCKED | no render submission after handler |
| Tabs conditional panel | BLOCKED | UI remains Home |
| Refresh state update | PASS | `android.render.submit ... revision=1 ok=1` |
| Detail push | PASS | `srf:1 -> srf:2`, then `srf:1 -> srf:3` |
| Detail in-page back | PASS | both routes reveal `srf:1` |
| Teardown process | PASS | app process stopped; no crash |
| Teardown zero-resource callback | UNVERIFIED | force-stop emits no final Runtime callback |

## Evidence files

- Tabs screenshots: `commerce-001-android-jsabi-category.png`,
  `commerce-001-android-jsabi-cart.png`, `commerce-001-android-jsabi-mine.png`
- Tabs log: `commerce-001-android-jsabi-tabs.log`
- Refresh screenshot/log/UI: `commerce-001-android-jsabi-refresh.png`,
  `commerce-001-android-jsabi-refresh.log`, `commerce-001-android-jsabi-refresh-ui.xml`
- Route screenshots/UI: `commerce-001-android-jsabi-detail-a.png`,
  `commerce-001-android-jsabi-home-a-ui.xml`, `commerce-001-android-jsabi-detail-b.png`,
  `commerce-001-android-jsabi-home-b-ui.xml`
- Route log: `commerce-001-android-jsabi-route-correct.log`
- Teardown log: `commerce-001-android-jsabi-teardown.log`

Representative successful refresh sequence:

```text
android.event.handler_execute surface=srf:1 handler=... dispatched=1
android.native.mount surface=srf:1 operations=8
android.render.submit surface=srf:1 transaction=txn:srf:1-1 revision=1 ok=1 error=
android.mount.result.received surface=srf:1 revision=1 ... mounted=1
```

Representative Tabs sequence, with no render submit afterward:

```text
android.input.tabs.change surface=srf:1 node=node:8 index=1 value=分类
android.event.change.received surface=srf:1 node=node:8
android.event.handler_execute surface=srf:1 handler=hdl:3 dispatched=1
```

No Core, JS, Toolkit, RPK, public Contract, or other platform was modified for
this validation.

