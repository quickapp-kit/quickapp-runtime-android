# QuickApp Runtime Android

[QuickApp Kit](https://github.com/quickapp-kit) 跨平台快应用引擎的 Android 平台适配层。

## 概述

`quickapp-runtime-android` 是 QuickApp 运行时的 Android 平台层，提供 **平台组合根**、**Runtime Host**、**JNI Gateway**、**Package Source** 和 **Platform Adapter** —— 将平台无关的 [quickapp-runtime-core](https://github.com/quickapp-kit/quickapp-runtime-core) 桥接到 Android 系统。

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
| `quickapp_android_host` | 组合根、运行时宿主、启动配置、包源、执行器 |
| `quickapp_android_platform` | 平台适配器 + 运行时骨架（集成 Core 与 JS 层） |
| `quickapp_android_runtime` | JNI 共享库（`libquickapp_android_runtime.so`） |

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

### 完整 Android 构建

```bash
./gradlew :app:assembleDebug
```

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
├── app/                        # Android 应用模块（Kotlin）
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
