# QuickApp Runtime Android

[QuickApp Kit](https://github.com/quickapp-kit) 跨平台快应用引擎的 Android 平台适配层。

## 概述

`quickapp-runtime-android` 包含 Android SDK Library 和最小 Host 示例。Library 提供稳定 Java Facade、JNI Gateway、C++ Runtime 组合和 Android Platform Adapter；Host 只选择本地 RPK 并提供 View 容器。

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

## 模块

| 组件 | 描述 |
|------|------|
| `quickapp-runtime-android` | Android Library，生成 AAR 和 JNI 共享库 |
| `quickapp-host` | 最小 APK，只负责本地 RPK 列表和 Surface 容器 |
| `quickapp_android_host` | AAR 内部的 C++ 组合根 |
| `quickapp_android_platform` | AAR 内部的平台适配器和 Runtime Spine |
| `quickapp_android_runtime` | 由 AAR 打包的 JNI 共享库 |

## 环境要求

- C++20 编译器（推荐 NDK r26+）
- CMake 3.24+
- Android Gradle Plugin 9.3+
- Gradle 8.x
- 兄弟仓库：`quickapp-runtime-core`、`quickapp-runtime-js`

## 构建

### Host 模块（契约测试，无需 Android 环境）

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

### SDK 和 Host 构建

```bash
./gradlew :quickapp-runtime-android:bundleReleaseAar
./gradlew :quickapp-host:assembleDebug
```

AAR 不包含 RPK；RPK 始终由 Host 或调用方传入。

### 验证（含 Sanitizer）

```bash
./tools/verify-and-s01.sh
```

## 项目结构

```
├── include/quickapp/android/   # C++ 公共头文件
│   ├── composition.h           # 组合根
│   ├── runtime_host.h          # 运行时宿主
│   ├── platform_adapter.h      # 平台适配器接口
│   ├── runtime_spine.h         # 运行时骨架（全链路集成）
│   ├── package_source.h        # 异步包源
│   ├── launch_profile.h        # 启动配置
│   ├── executor.h              # 任务执行器
│   └── jni_gateway.h          # JNI 入口
├── src/                        # C++ 实现
├── runtime/                    # Android Library 模块和 Java Facade
├── app/                        # 最小 Host APK 模块
├── tests/                      # 契约测试
├── cmake/                      # CMake 工具
├── tools/                      # 验证脚本
└── evidence/                   # 实现验证文档
```

## 相关仓库

- [quickapp-runtime-core](https://github.com/quickapp-kit/quickapp-runtime-core) — 平台无关 C++ 运行时内核
- [quickapp-runtime-js](https://github.com/quickapp-kit/quickapp-runtime-js) — JS 引擎集成层

## 许可证

本项目基于 [MIT 许可证](LICENSE) 开源。
