# commerce-001 Android latest RPK reload v4 (2026-08-26)

## Conclusion

The newly rebuilt `commerce-001.rpk` was copied into the Android APK, installed,
and launched on `emulator-5554`. The updated visual layout is active: Home shows
the header, Image assets, product cards, detail buttons, scroll content, and
bottom Tabs.

The category interaction is still partial. Android changes the native Tabs
selection and the event reaches the JS `onTabChange` handler, but the handler
does not produce a follow-up `RenderTransaction`. The UI therefore continues to
show `当前栏目：首页` and Home content. No Core, JS, Toolkit, Contract, or
example source was modified during this reload.

## Input and verification

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- RPK SHA-256: `97905939e1c0d9cd77bf642bde1d95c69279f34cfdac3f6a9d498a3328c724e1`
- Android asset SHA-256: `97905939e1c0d9cd77bf642bde1d95c69279f34cfdac3f6a9d498a3328c724e1`
- APK SHA-256: `27968cc5fa67ff081ae89a95abc311d007cdf0f11569568eac6a29666de2bc63`
- Build: `./gradlew :app:assembleDebug --no-daemon`
- Install: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- Launch: `adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk commerce-001.rpk`

## Results

| Check | Result |
| --- | --- |
| Latest RPK included in APK | PASS |
| Home RPK load and initial mount | PASS |
| Updated cards, Image, Text, Button | PASS |
| Scroll content and Tabs | PASS |
| Native category selection | PASS |
| Category conditional content | BLOCKED: remains Home |
| Event -> JS -> incremental render | BLOCKED: no `android.render.submit` |

## Evidence

- Home screenshot: `commerce-001-android-home-v4.png`
- Category screenshot: `commerce-001-android-category-v4.png`
- Home UI dump: `commerce-001-android-home-v4-ui.xml`
- Category UI dump: `commerce-001-android-category-v4-ui.xml`
- Category log: `commerce-001-android-category-v4.log`

Observed event sequence:

```text
android.input.tabs.change surface=srf:1 node=node:8 index=1 value=分类
android.event.change.received surface=srf:1 node=node:8
android.event.js_callback posted=1 error=
android.event.change.dispatched=1
android.event.handler_execute surface=srf:1 handler=hdl:3 dispatched=1
```

No `android.render.submit` follows the handler. This remains a public reactive
render-path issue, not an RPK loading or Android input delivery issue.

