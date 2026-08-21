# AND-S01 Isolated Implementation Evidence

## Conclusion

The Android host contract implementation is verified in isolation by normal and
sanitizer builds. It proves composition rejection behavior, fixed-resource
package reads, root-presented gating, lifecycle error forwarding, and
deterministic teardown without JNI, View, Mount, or Input code. Real APK/native
composition evidence remains pending AND-S08/AND-S09.

## Reproduce

```sh
./tools/verify-and-s01.sh
```

## Verified Evidence

| Area | Evidence |
|---|---|
| Composition | Strict profile/manifest decoders; Fake inventory mismatch rejection for selected engine, module identities, `runtime.js-framework`, and `binaryBytes`; this is not real link evidence |
| Package | Memory, file, and Asset-reader sources; immutable bytes; random reads; zero length; out-of-range; asynchronous Core completion; fixed file descriptor from open through close |
| File identity | Replacing the path after open still reads the original resource; truncation returns typed `PACKAGE_IO_ERROR`; queued read/close completes exactly once on the Core queue |
| Startup | Host remains `starting` until Fake Core returns Root `presented` |
| Identity | `AppRuntimeCreateRequest` has no `AppRuntimeId`; Fake Core Factory generates it |
| Lifecycle | Core `LIFECYCLE_BUSY` is returned unchanged; Host stores no foreground/background or route state |
| Teardown | Normal destroy, concurrent destroy, destroy during startup, Root failure, and Core destroy failure converge on local release |
| Observation | Noop and Recording Sink selection produce the same Core call order and results |
| Boundary | Symbol scan finds no JNI, SurfaceView, MountTransaction, or PlatformInputMessage implementation |
| Memory safety | AddressSanitizer and UndefinedBehaviorSanitizer pass all contract groups |

## Test Output

```text
PASS strict decoders and composition
PASS package sources
PASS file identity and read/close race
PASS root presented and Core identity
PASS lifecycle busy and destroy
PASS failure and destroy races
PASS Noop/Recording equivalence
SUMMARY 7/7 contract groups passed
```

## Integration Evidence Still Required

The current target is the isolated Android Host library with Fake Core/JS
dependencies. Its supplied inventory only exercises Composition Root validation.
A real APK/native-library link map must still prove that the final product
contains one shared Core, one JS Framework, exactly one selected Engine, and no
unselected modules. AND-S08/AND-S09 own that integration evidence.
