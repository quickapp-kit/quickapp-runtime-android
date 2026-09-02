# QuickApp Runtime Android

Android platform adapter for the [QuickApp Kit](https://github.com/quickapp-kit) cross-platform quick-app engine.

## Overview

`quickapp-runtime-android` contains the Android SDK library and a minimal Host sample. The library provides the public Java facade, JNI gateway, C++ runtime composition and Android platform adapter. The Host only selects local RPK files and supplies a View container.

```
┌──────────────────────────────────────────────────┐
│              Android App (Kotlin/Java)            │
├──────────────────────────────────────────────────┤
│        quickapp-runtime-android (C++20)           │
│  ┌────────────┬─────────────┬──────────────────┐ │
│  │ Runtime    │  Package    │  Platform        │ │
│  │ Host       │  Source     │  Adapter         │ │
│  ├────────────┼─────────────┼──────────────────┤ │
│  │ Composition│  Launch     │  JNI Gateway     │ │
│  │ Root       │  Profile    │  (shared lib)    │ │
│  └────────────┴─────────────┴──────────────────┘ │
├──────────────────────────────────────────────────┤
│        quickapp-runtime-core (C++20)              │
│   (Foundation / Package / Runtime Tree / Render)  │
├──────────────────────────────────────────────────┤
│        quickapp-runtime-js (C++20)                │
│   (JS Engine / Module Loader / Page Host)         │
└──────────────────────────────────────────────────┘
```

## Modules

| Component | Description |
|-----------|-------------|
| `quickapp-runtime-android` | Android Library; publishes the AAR and JNI shared library |
| `quickapp-host` | Minimal APK; local RPK catalog and Surface container only |
| `quickapp_android_host` | C++ composition root inside the AAR |
| `quickapp_android_platform` | Platform adapter + runtime spine inside the AAR |
| `quickapp_android_runtime` | JNI shared library packaged by the AAR |

## Requirements

- C++20 compiler (NDK r26+ recommended)
- CMake 3.24+
- Android Gradle Plugin 9.3+
- Gradle 8.x
- Sibling repos: `quickapp-runtime-core`, `quickapp-runtime-js`

## Build

### Host-only (contract tests, no Android)

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

### SDK and Host build

```bash
./gradlew :quickapp-runtime-android:bundleReleaseAar
./gradlew :quickapp-host:assembleDebug
```

The AAR never contains RPK files. RPK files are Host or caller inputs.

### Verification (with sanitizers)

```bash
./tools/verify-and-s01.sh
```

## Project Structure

```
├── include/quickapp/android/   # Public C++ headers
│   ├── composition.h           # Composition root
│   ├── runtime_host.h          # Runtime host
│   ├── platform_adapter.h      # Platform adapter interface
│   ├── runtime_spine.h         # Runtime spine (full integration)
│   ├── package_source.h        # Async package source
│   ├── launch_profile.h        # Launch profile config
│   ├── executor.h              # Task executor
│   └── jni_gateway.h          # JNI entry point
├── src/                        # C++ implementation
├── runtime/                    # Android Library module and public Java facade
├── app/                        # Minimal Host APK module
├── tests/                      # Contract tests
├── cmake/                      # CMake utilities
├── tools/                      # Verification scripts
└── evidence/                   # Implementation evidence
```

## Related Repositories

- [quickapp-runtime-core](https://github.com/quickapp-kit/quickapp-runtime-core) — Platform-independent C++ runtime core
- [quickapp-runtime-js](https://github.com/quickapp-kit/quickapp-runtime-js) — JS engine integration layer

## License

This project is licensed under the [MIT License](LICENSE).
