# Android B4 Platform Feature Evidence

## 结论

Android Provider 已实现并接入现有 typed Feature 边界，Provider 单测通过，真实 `platform-001.rpk` 已加载并完成首屏与 Prompt `completed` 回调验证。File/Fetch 的 Android Provider 逻辑已覆盖，但当前真实 RPK 的可选字段在 JS ABI 入口被拒绝，导致这两次请求没有到达 Android Provider；该阻塞位于冻结的 JS Facade/ABI 公共层，本轮未修改。

## 范围

- 只修改 `quickapp-runtime-android/**`。
- Prompt、Fetch、File 通过 Core `ModuleRegistry` 注册同一个 Android Provider。
- Fetch 只接受 deterministic `local://platform/*` URL，不访问网络。
- File 使用内存私有 Provider，路径必须位于 `private/`，不访问外部文件系统。
- Provider 支持 `completed`、`failed`、`unsupported`、`cancelled`。
- Surface destroy 调用 Provider teardown；没有第二套路由、第二棵 Tree 或平台业务状态。

## RPK

- Source: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/platform-001/dist/platform-001.rpk`
- SHA-256: `79ace8e7a28eeef67c31ae3cb519af7c7e3a85c8556c8ecb4811456f3a49035d`
- Android asset: `app/build/generated/case001-assets/platform-001.rpk`

## 修改文件

- `include/quickapp/android/feature_provider.h`
- `src/feature_provider.cpp`
- `src/runtime_spine.cpp`
- `CMakeLists.txt`
- `app/build.gradle.kts`
- `app/src/main/java/dev/quickapp/kit/android/MainActivity.java`
- `tests/b4_feature_provider_tests.cpp`
- `evidence/b4-platform-feature-android.md`

## Provider 状态矩阵

| 场景 | Provider 输入 | 结果 |
|---|---|---|
| Prompt confirm | non-empty text | `completed`, `confirmed=true` |
| Fetch success | `local://platform/status` | `completed`, HTTP 200, deterministic body |
| Fetch failure | `local://platform/failure` | `failed`, `FETCH_DETERMINISTIC_FAILURE` |
| Fetch cancellation | `local://platform/cancelled` | `cancelled` |
| Unknown Fetch URL | other URL | `unsupported` |
| File read | `private/platform-state.txt` | `completed`, in-memory data |
| File path escape | `../outside.txt` or non-private path | `failed`, `FILE_PATH_REJECTED` |
| Missing Provider | empty `ModuleRegistry` | `unsupported` |
| Registry close | closed registry | `failed`, `CAPABILITY_CLOSED` |

## Host 验收

```text
cmake --build build-host --target and_b04_feature_provider_tests -j2
ctest --test-dir build-host --output-on-failure -R 'and_(s01|b04)'
```

结果：`2/2 passed`。

覆盖：Prompt completed、Fetch failed、Fetch cancelled、private File read、路径拒绝、无 Provider unsupported、teardown。

## Android 验收

构建和安装：

```text
./gradlew :app:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.quickapp.kit.android/.MainActivity \\
  --es quickapp.rpk platform-001.rpk
```

APK 构建成功，APK 路径：
`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/app/build/outputs/apk/debug/app-debug.apk`

真实日志和截图：

- `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/evidence/platform-001-android.log`
- `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/evidence/platform-001-android-home.png`
- `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/evidence/platform-001-android-after-actions.png`

已验证：

- RPK verified；`feature.registry.created`。
- Home Surface 创建、Page JS 加载、三条 Handler 绑定、首屏 Mount、Present 均成功。
- Prompt 点击真实经过 `click -> JS handler -> Android Feature Provider`，日志为：
  `android.feature.result ... module=0 method=2 status=completed`。

## 当前阻塞

同一工作区的后续增量构建被未修改的公共文件阻塞：

```text
quickapp-runtime-core/src/page_ir.cpp:591
error: expected ')'
```

在已安装的本轮 APK 中，File/Fetch 点击出现 `ABI_INVALID_ARGUMENT`，且日志没有对应 `android.feature.result`；这说明请求在 JS ABI 入口被拒绝，尚未到达 Android Provider。Android 侧不能通过旁路修复，也未修改 Core、JS、Toolkit、公共 Contract 或 Examples Composition Root。

因此本文件区分两件事：Android Provider 的 typed 状态矩阵已由 Android host test 验证；真实 RPK 的 Prompt 已完成端到端验证，File/Fetch 端到端回归需待公共 JS/Core 工作区恢复可编译并修复 ABI 可选字段入口后再补验。

## JS/Core 修复后的补充验证（2026-08-25）

公共 JS ABI 已修复：QuickJS 普通对象根级属性值为 `undefined` 时按字段未提供处理，必填字段缺失、`null`、错误类型和嵌套非法结构仍拒绝。Android Provider 业务代码未修改。

最终 APK 构建和真实模拟器点击使用同一 RPK：

- RPK：`quickapp-examples/showcases/platform-001/dist/platform-001.rpk`
- 大小：`17171` bytes
- SHA-256：`79ace8e7a28eeef67c31ae3cb519af7c7e3a85c8556c8ecb4811456f3a49035d`
- 日志：`evidence/platform-001-android-b4-fixed-actions.log`
- Prompt 日志：`evidence/platform-001-android-b4-fixed-prompt.log`
- 截图：`evidence/platform-001-android-b4-fixed-actions.png`

补充结果：真实点击 File 产生 `android.feature.result ... module=4 method=6 status=completed`；真实点击 Fetch 产生 `android.feature.result ... module=3 method=4 status=completed`。因此原先“请求停留在 JS ABI、未到达 Provider”的阻塞已解除。
