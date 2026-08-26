# commerce-001 Android Platform Acceptance

## Conclusion

The real `commerce-001.rpk` completed Android RPK loading, initial mount,
Image/Text/Button rendering, string/boolean state refresh, keyed product blocks,
Core Router push/back, repeated detail entry, and event delivery.

The acceptance is **PARTIAL**, with two shared/runtime boundaries recorded:

1. Numeric controlled Tabs state is not accepted by the shared JS ABI
   `updateBinding` validator, so the native selected label changes but the
   bound `selectedValue` and `if` content remain Home.
2. Activity teardown logs `destroy.begin surfaces=3 nodes=49`, then the process
   exits before the final `runtime.stopped` zero-resource callback. No crash was
   observed, but resource zero cannot be claimed from this run.

No Android workaround was added for either condition.

## Input and build

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- SHA-256: `0eaeb9a71f11c2119c86119f253af122511867eb5b388fa682b14719456a9a4f`
- Build: `./gradlew :app:assembleDebug --no-daemon`
- Install: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- Launch: `adb shell am start -n dev.quickapp.kit.android/.MainActivity --es quickapp.rpk commerce-001.rpk`
- Device: `emulator-5554`

## Results

| Capability | Result | Evidence |
| --- | --- | --- |
| RPK verify and page VM | PASS | `commerce-001-android-start.log`: `rpk.verified`, `page.vm.ready` |
| Home layout | PASS | `commerce-001-android-home.png` |
| Image/Text/Button | PASS | Home screenshot and UI dump in start log session |
| Tabs native interaction | PARTIAL | `分类` selected style changed; `change` reached JS |
| Tabs controlled binding | BLOCKED | Numeric `updateBinding` rejected by shared JS ABI |
| String/boolean state update | PASS | Refresh produced `revision=1`, `operations=11`, `mount.result ok=true` |
| Keyed product blocks | PASS | Three product block handlers bound; refresh transaction completed |
| Product detail push | PASS | `srf:1 -> srf:2`, ProductDetail VM and mount completed |
| Detail back | PASS | `navigation.close`, `close.result`, reveal `srf:1` |
| Repeated detail entry | PASS | `srf:1 -> srf:3`, second ProductDetail mounted |
| Repeated detail back | PASS | `srf:3 -> srf:1`, close completed |
| Runtime teardown | INCOMPLETE EVIDENCE | `destroy.begin surfaces=3 nodes=49`; no final zero callback |

## Interaction sequence

1. Launch Home and capture `commerce-001-android-home.png`.
2. Tap the second Tabs item `分类`; capture `commerce-001-android-category.png`.
3. Tap `切换推荐状态`; capture `commerce-001-android-refresh.png`.
4. Tap the second product `查看商品`; capture `commerce-001-android-detail-1.png`.
5. Tap `返回首页`; capture `commerce-001-android-home-after-back-1.png`.
6. Tap the first product `查看商品` again; capture `commerce-001-android-detail-2.png`.
7. Tap `返回首页` again; capture `commerce-001-android-home-after-back-2.png`.

## Logs and screenshots

- Start: `evidence/commerce-001-android-start.log`
- First interaction: `evidence/commerce-001-android-interaction-1.log`
- Full interaction: `evidence/commerce-001-android-interaction-full.log`
- Teardown: `evidence/commerce-001-android-teardown.log`
- Detail UI dump: `evidence/commerce-001-android-detail-ui.txt`
- Second detail UI dump: `evidence/commerce-001-android-detail-2-ui.txt`
- Screenshots: `commerce-001-android-home.png`, `commerce-001-android-category.png`,
  `commerce-001-android-refresh.png`, `commerce-001-android-detail-1.png`,
  `commerce-001-android-home-after-back-1.png`, `commerce-001-android-detail-2.png`,
  `commerce-001-android-home-after-back-2.png`

## Boundary findings

The Tabs event path is valid:
`Android input -> Core Event Router -> JS onTabChange`.
The platform label changes, but `quickapp-runtime-js/src/abi/runtime_abi_codec.cpp`
still rejects numeric `updateBinding.value`; therefore the page's numeric
`selectedTab` cannot produce the expected controlled text/if update. This is a
shared JS/ABI issue and was not modified here.

The teardown run observed no `AndroidRuntime` crash. It did not produce
`android.runtime.stopped ... surfaces=0 nodes=0`, so this evidence does not
claim resource zero. The existing Android teardown path was not bypassed or
replaced.

Only `quickapp-runtime-android` was used for this acceptance. Core, JS,
Toolkit, public Contract, Examples, and other platforms were not modified.
