# B1 Input + Switch Android Evidence

## Conclusion

Android B1 native control mapping is implemented and verified against the real
`controls-001.rpk` package. The platform sends native control events through the
existing JNI -> Core Event Router -> JS Handler path.

## RPK

- Path: `quickapp-examples/showcases/controls-001/dist/controls-001.rpk`
- SHA-256: `1e25a27daf59e5ae2f6b0bd046a7e9c4fe2876cfcd4dcebea2a2e26a7ed7f829`

## Platform changes

- `RuntimeSurfaceHost.java`: Android `EditText` and `Switch` host mappings,
  native event listeners, controlled-property suppression, and focus transfer.
- `RuntimeBridge.java`, `NativeGateway.java`, `runtime_spine.h`,
  `jni_gateway.cpp`, `runtime_spine.cpp`: typed platform event forwarding with
  `value` and `checked` payloads through the existing Core event port.
- `MainActivity.java`, `app/build.gradle.kts`: Android-local RPK asset entry.

No Core, JS, Toolkit, public Contract, or Example source was modified.

## Build and launch

```text
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.quickapp.kit.android/.MainActivity \
  --es quickapp.rpk controls-001.rpk
```

Build result: `BUILD SUCCESSFUL`.

## Runtime verification

- RPK verified and Home page module loaded.
- Initial Mount: `operations=56`, `ok=true`, `revision=0`.
- Surface presented: `srf:1`.
- Five handlers bound: `onInput`, `onChange`, `onFocus`, `onSwitch`, `onRefresh`.
- Input produced `focus`, repeated `input`, then `change`; each was dispatched
  by Core and executed by the expected JS handler.
- Switch produced `change` with the boolean `checked` payload; the JS handler
  executed successfully.
- Refresh button produced `click`; the JS handler executed successfully.
- Android controlled property updates suppress native feedback loops.

Evidence:

- Initial screen: `evidence/controls-001-android-home.png`
- Interactive screen: `evidence/controls-001-android-final.png`
- Event log: `evidence/controls-001-android-interaction.log`

## Scope boundary

The real `controls-001.rpk` manifest contains only `pages/Home`. It has no
Detail route or navigation handler. Therefore push/back, repeated Detail entry,
and route teardown are `NOT_APPLICABLE` for this RPK; no platform-local route or
synthetic page was introduced to manufacture those results. Existing Android
Gallery/Consumer showcase evidence remains the route regression baseline.
