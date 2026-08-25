# Android B3 List + Scroll

日期：2026-08-25

## 结论

Android 平台的 B3 `List + Scroll` 适配已完成，平台实现复用既有 MountTransaction、Runtime Tree、Core Event Router 和 Lifecycle。没有创建平台私有列表状态、第二棵树或第二套路由。

真实 APK 回归尚未完成：当前全量 Android APK 链接被共享 `quickapp-runtime-js/src/abi/runtime_abi_codec.cpp` 的既有编译错误阻塞。本任务没有修改该共享文件。

## RPK 基线

- 源 RPK：`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/list-001/dist/list-001.rpk`
- SHA-256：`f9087a6e1a9b0cc9c104a57586b6196636b8a2853d386ab68551fa2c0eb640c2`
- Android 生成资产：`app/build/generated/case001-assets/list-001.rpk`
- 生成资产 SHA-256：与源 RPK 一致

真实 RPK 已确认包含：

- `Scroll` Host Component
- `List` Host Component
- keyed `for`，key 为 `item.id`
- `scroll`、`scrollend`、`scrolltop`、`scrollbottom`
- 图片资源和列表项点击 Handler

## Android 实现

- `Scroll` 映射为 Android `ScrollView`，由真实 View 层提供滚动范围和裁剪。
- `List` 映射为 Android `FrameLayout` 容器，列表项仍由 Core MountTransaction 创建、插入、移动和删除。
- 滚动事件通过现有 `RuntimeSurfaceHost -> RuntimeBridge -> JNI -> RuntimeSpine -> Core EventRouter -> JS Handler` 链路传递。
- 滚动 payload 包含 `scrollOffset`、`contentSize`、`viewportSize`。
- 顶部/底部事件只在边界状态变化时产生；滚动停止后产生 `scrollend`。
- ScrollView 销毁时解除滚动监听并取消延迟的 `scrollend` 回调。
- Android 入口和资产同步已加入 `list-001.rpk`，未修改案例 DSL。

## 编译验证

通过：

```text
./gradlew :app:compileDebugJavaWithJavac
ninja -C app/.cxx/Debug/3t34v626/x86_64 quickapp_android_platform -j2
```

其中 native target 的单独编译通过：

- `quickapp-runtime-android/src/runtime_spine.cpp`
- `quickapp-runtime-android/src/jni_gateway.cpp`
- `quickapp-runtime-android/src/platform_adapter.cpp`

未完成：

```text
./gradlew :app:assembleDebug
```

失败发生在共享 JS ABI 编译，不是 Android B3 文件：

```text
quickapp-runtime-js/src/abi/runtime_abi_codec.cpp
```

当前错误包括 `FeatureRequest/FeatureResult` switch 未覆盖和已有指针调用错误。按任务边界没有修改 Core、JS、Toolkit 或公共 Contract。

## 真实设备验收状态

以下项目必须在共享 JS ABI 修复并重新生成 APK 后执行，当前不宣称已通过：

- list-001 首屏
- keyed for 列表可见
- ScrollView 实际滚动
- scroll/scrollend/scrolltop/scrollbottom 到 JS Handler
- 列表项点击
- 重复进入和页面返回
- teardown 后 surfaces/nodes/handlers 归零

当前可用的 Android 模拟器为 `emulator-5554`，但本轮没有安装新 APK；现有 `app-debug.apk` 不能作为本次 B3 验收证据。

## 未修改范围

- 未修改 `quickapp-runtime-core/**`
- 未修改 `quickapp-runtime-js/**`
- 未修改 `quickapp-toolkit/**`
- 未修改 `quickapp-examples/**`
- 未修改公共 Contract
