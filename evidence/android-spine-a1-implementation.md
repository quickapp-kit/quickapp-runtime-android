# Android Spine A1 Evidence

## 目录

- [结论](#结论)
- [验收链路](#验收链路)
- [实现边界](#实现边界)
- [复现](#复现)
- [证据](#证据)
- [限制](#限制)

## 结论

Android Spine A1 已通过：真实联盟基线 RPK 可以由 Android Host 加载，经共享 JS Runtime 和共享 C++ Core，在 Android View 上完成首屏渲染；点击真实 Button 后，事件通过 typed JNI/Runtime ABI 回到 JS Handler，JS Router 产生 typed NavigationPush，由 Core 创建并呈现详情页；正常退出后资源归零。

这证明的是 Android 平台接入骨架和三大系统的第一条真实闭环，不代表 Android 全量能力或 V1 全量完成。

## 验收链路

```text
tk-s07-case001.rpk
  -> Android Asset -> Memory PackageSource -> RPK Loader
  -> shared QuickJS + JS Framework
  -> typed JsCoreIngress -> shared C++ Core
  -> Core InitialRenderIntent -> MountTransaction
  -> Android UI thread View/Text/Button
  -> Button node:3 click
  -> PlatformInputMessage(req:p-*)
  -> JsEventDispatch -> JS Handler
  -> NavigationPush(req:j-100001, /pages/DemoDetail)
  -> Core Navigation -> srf:2
  -> Detail page MountTransaction -> Android Present(push)
```

## 实现边界

| 层 | A1 责任 | 关键实现 |
|---|---|---|
| Android Host | Activity 生命周期、RPK asset 复制、JNI 回调入口 | `app/src/main/java/dev/quickapp/kit/android/` |
| Android Platform Adapter | UI thread 上创建/更新/呈现 View；产生点击输入 | `RuntimeSurfaceHost.java`、`RuntimeBridge.java` |
| JNI | typed message 和线程桥接 | `src/jni_gateway.cpp`、`include/quickapp/android/` |
| C++ Android Composition | 选择并组装共享 Core/JS，维护 Runtime thread | `src/runtime_spine.cpp` |
| C++ Core | Runtime Tree、Mount、Surface、Navigation、Lifecycle 的唯一所有者 | `quickapp-runtime-core` |
| JS Runtime | RPK 模块加载、页面 VM、Handler、Router API | `quickapp-runtime-js` |

Android 代码没有把 Android 类型带入 Core，也没有创建本地路由、第二棵 Runtime Tree 或通用 JSON Bridge。

## 复现

### Native build

```sh
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android
cmake --build build-android-arm64 -j 8
```

### APK

最终验收 APK：

```text
build-manual-apk/quickapp-android-a1-v31.apk
SHA-256: bada410b5a327d44fb5d43440f2770f59e451acafdf69b3994d7146bfded5c6b
```

安装并启动：

```sh
adb install -r build-manual-apk/quickapp-android-a1-v31.apk
adb shell monkey -p dev.quickapp.kit.android 1
```

等待首屏后点击屏幕底部绿色按钮；A1 使用真实 Android Button 的点击回调，不是测试旁路调用。

## 证据

### Build

- Android ABI：`arm64-v8a`
- NDK：`28.2.13676358`
- Android platform：`android-36.1`
- build-tools：`36.0.0`
- CMake/Ninja native build：通过
- APK v3 签名校验：通过
- Gradle：未作为本次构建前提；Gradle 依赖下载受 Maven 网络不可用阻塞，因此使用本地已有 Android 工具完成 APK 组装

### Runtime log

关键日志事实：

```text
android.stage=rpk.verified
android.js.module id=@quickapp-kit/app kind=app status=loaded
android.native.mount surface=srf:1 operations=18
android.initial.result surface=srf:1 prepared=1
android.native.present ... surface=srf:1 push=0
android.input.click surface=srf:1 node=node:3
android.event.js_callback posted=1 error=
android.navigation.push request=req:j-100001 source=srf:1 uri=/pages/DemoDetail accepted=1
android.native.mount surface=srf:2 operations=28
android.native.present ... surface=srf:2 push=1
android.surface.operation kind=1 request=req:j-100001 target=srf:2 error=
android.runtime.stopped surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0
```

### Screenshots

- 文案更新后的详情页：`build-manual-apk/android-a1-v32-after-click.png`
- A1 首屏与点击验证：`build-manual-apk/android-a1-v31-after-click.png`
- 上一轮独立首屏截图：`build-manual-apk/android-a1-v30-current.png`

### Fixture

```text
quickapp-toolkit/evidence/tk-s07-case001.rpk
SHA-256: 32e012e2235c7ffa36143d9619c90264bbbab5ae0d083e12a13092859990b493
```

## 限制

1. 当前只在 Android arm64 Emulator 上验证，未覆盖实体 Android 设备、不同 density 和旋转配置。
2. A1 只覆盖 View/Text/Button、首屏、点击、单次 NavigationPush 和 teardown。
3. Gradle 在线依赖和标准 Android Studio 构建链尚未恢复；这不影响本次 native/runtime 行为验证，但后续需要补标准 Gradle 构建证据。
4. 还未验证返回、增量 RenderTransaction、更多组件、Capability Provider、Measure Adapter 和完整观察采集。

## Android Golden RPK P0 复用检查

日期：2026-08-24

状态：`ANDROID_P0_BUILD_BLOCKED`

本轮只修改 Android 工程，未修改 Core、JS Framework、Toolkit、Examples 或 LVGL。

已完成：

- Android Gradle asset 输入已切换为 `tk-s12-lvgl-p0.rpk`，不再使用旧 `tk-s07-case001.rpk`。
- Android JNI Mount 映射已适配冻结的 `MoveHost.new_parent_node_id` 和 `RemoveHost`。
- Android Runtime Spine 已消费冻结 ABI 中的初始 block、`InstantiateBlock`、`RemoveBlock` 和 `MoveBlock`。
- Android ARM64 Native CMake build 通过：`build-android-arm64`。
- Host-only CMake/CTest 通过：`and_s01_contract_tests`，`1/1 passed`。
- Golden RPK 存在且 SHA-256 校验通过：

```text
../quickapp-toolkit/evidence/tk-s12-lvgl-p0.rpk
25977ea6d92ed571ed6d019c3b0dc0b3ee5f1576acdf1ac3ee98fa68244ed74b
```

当前阻塞：

- 工程没有 `gradlew` wrapper。
- 系统 Gradle 启动失败：`Failed to load native library 'libnative-platform.dylib' for Mac OS X aarch64`。
- 因此尚未生成使用新 Android Java 入口和 Golden RPK 的 APK，未宣称 Android UI P0 通过。

尚未验证：

- Android Home 首屏实际显示。
- Android state/if/keyed for 可见更新。
- Android Detail push/back。
- Android UI teardown 资源归零。
- Android Feature Provider。
