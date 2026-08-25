# B2 Slider + Picker Android Evidence

## Conclusion

Android B2 native control mapping is implemented and verified with the real
`controls-002.rpk` package. Slider and Picker events use the existing Android
JNI -> Core Event Router -> JS Handler path.

## RPK

- Path: `quickapp-examples/showcases/controls-002/dist/controls-002.rpk`
- SHA-256: `b738c890107d54f82ecf2c3f949c5df3688b6760e45d326b08f4c23de53d297a`

## Platform implementation

- `Slider` maps to Android `SeekBar`.
- `min`, `max`, `step`, and `value` are applied from MountTransaction.
- User movement is quantized to the declared step and emits `change` with
  `{ value: number, isFromUser: true }`.
- Controlled `value` updates are suppressed from producing a native feedback
  event.
- Text `Picker` maps to an Android `TextView` plus a native `AlertDialog`.
- The dialog supports selection, `取消`, and `确认`.
- Cancel closes without dispatching `change`; confirm updates the displayed
  text and emits `{ selected: number, value: string }`.
- No second route, tree, state store, or platform-side application logic was
  introduced.

## Build and launch

```text
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.quickapp.kit.android/.MainActivity \
  --es quickapp.rpk controls-002.rpk
```

Build result: `BUILD SUCCESSFUL`.

## Verification

- RPK verified and `@quickapp-kit/page/pages/Home` loaded.
- Initial Mount: `operations=58`, `ok=true`, `revision=0`.
- Surface `srf:1` presented successfully.
- Slider initial value was `40`; a real drag generated step-aligned values
  including `45`, `50`, `55`, `65`, `70`, `75`, `80`, `85`, and `95`.
- Each Slider event was accepted by Core and executed by `hdl:1` (`onSlider`).
- Picker opened as a native dialog with `安静`, `标准`, `性能`, `取消`, and `确认`.
- Cancel path produced no Picker change event.
- Confirming `性能` produced:
  `android.input.picker.change surface=srf:1 node=node:7 selected=2 value=性能`.
- The event was accepted by Core and executed by `hdl:2` (`onPicker`).
- Final screen displayed Picker value `性能`.
- Activity back teardown reported:
  `surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0`.

Evidence files:

- `evidence/controls-002-android-home.png`
- `evidence/controls-002-android-slider.png`
- `evidence/controls-002-android-picker-open-verified.png`
- `evidence/controls-002-android-picker-confirmed.png`
- `evidence/controls-002-android-final.png`
- `evidence/controls-002-android-interaction.log`

## Scope boundary

The real `controls-002.rpk` manifest declares only `pages/Home`; it has no
Detail route, navigation handler, or second page. Therefore route push/back and
repeated page entry are `NOT_APPLICABLE` for this RPK, not failed. Existing
Gallery/Commerce showcase cases remain the route regression baseline.

No Core, JS, Toolkit, public Contract, or Examples source was modified.
