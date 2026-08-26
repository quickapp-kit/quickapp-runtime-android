# commerce-001 Android latest RPK reload (2026-08-26)

## Conclusion

The latest `commerce-001.rpk` was rebuilt into the Android asset, installed, and
launched on `emulator-5554`. The RPK is the new example output, not a stale APK
asset. Home renders successfully with the header, Image nodes, 20 product rows,
scroll container, refresh button, and Tabs.

Tabs input is only partially successful: Android changes the native selected
state and dispatches `change` to the JS handler, but no follow-up
`RenderTransaction` is submitted. The page therefore remains on the Home
panel (`当前栏目：首页`) instead of switching to `分类`. This run does not
modify the example, Core, JS, Toolkit, or public Contract.

## Inputs and commands

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- RPK SHA-256: `91c33ac1b017c0b473d5798319e0049cc85c8dcbc4de3c16cab26790ed8e55b5`
- Android asset SHA-256: `91c33ac1b017c0b473d5798319e0049cc85c8dcbc4de3c16cab26790ed8e55b5`
- APK SHA-256: `7cf9e8fffd486a80006d054f82175390849ce87649728cddf9e2cda9090c1a78`
- Build: `./gradlew :app:assembleDebug --no-daemon`
- Install: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- Launch: `adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk commerce-001.rpk`

## Results

| Check | Result |
| --- | --- |
| New RPK copied into APK asset | PASS |
| RPK verify and Home VM | PASS |
| Home header, Image, Text, Button | PASS |
| 20 product rows and ScrollView | PASS |
| Native Tabs selection | PASS: `分类` selected |
| Tabs JS state and conditional panel | BLOCKED: page remains Home |
| Tabs render submission | BLOCKED: no `android.render.submit` after handler |

## Evidence

- Screenshot: `commerce-001-android-category-v3.png`
- UI dump: `commerce-001-android-category-v3-ui.xml`
- Log: `commerce-001-android-category-v3.log`
- Start UI dump: `commerce-001-android-home-v3-ui.txt`

Key log sequence:

```text
android.input.tabs.change surface=srf:1 node=node:7 index=1 value=分类
android.event.change.received surface=srf:1 node=node:7
android.event.js_callback posted=1 error=
android.event.change.dispatched=1
android.event.handler_execute surface=srf:1 handler=hdl:3 dispatched=1
```

No `android.render.submit` follows this handler. The visible screenshot still
contains `当前栏目：首页` and the Home product list. This separates successful
platform input delivery from the missing JS reactive render submission.

