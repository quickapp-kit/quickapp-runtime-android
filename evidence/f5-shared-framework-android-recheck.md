# F5 Android Shared Framework 冷构建复核

## 结论

本次复核未通过 Shared Framework 完整运行验收。

冷清理、冷构建、重新安装和真实 RPK 校验均通过；Android 日志明确证明 `@quickapp-kit/framework-v1 kind=shared` 已从 RPK 加载。但 Home 页面 VM 在 `onInit` 调用页面声明的 `$app_require$` 时失败：`require outside module evaluation`。因此未进入首屏 Mount、状态更新、事件或路由验收。本轮没有修改任何公共代码，按约束停止并交给 Core/JS Agent。

## 清理范围

在 `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android` 删除后重新生成：

- `.gradle/`
- `build/`
- `app/build/`
- `runtime/build/`
- `app/.cxx/`
- `runtime/.cxx/`

未删除源码、RPK、公共仓库或平台逻辑。

## 构建与安装

```text
./gradlew :quickapp-host:assembleDebug --no-daemon --no-configuration-cache --console=plain
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am force-stop dev.quickapp.kit.host
adb shell am start -S -n dev.quickapp.kit.host/.MainActivity
```

结果：Gradle `BUILD SUCCESSFUL`，58 个任务全部执行；APK 安装返回 `Success`。

APK SHA-256：`bc95f1e1117816c711a7916669a07f4b188e9fdf88502db8fc0a4d231c521b27`

Native ABI：APK 同时包含 `arm64-v8a` 与 `x86_64` 的 `libquickapp_android_runtime.so`。

Native binary SHA-256：

- `arm64-v8a`: `ea82c16fc706a70b75e4381c4bd4a3c3f77f24ac4a02db890255e76955397073`
- `x86_64`: `0af956e1aa36295dd0996cd726a2109d82e18f666c17d0c3bf80d97fadb00240`

Android 进程实际加载日志包含：

```text
Load .../lib/arm64/libquickapp_android_runtime.so ...: ok
```

## RPK 校验

源文件：`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/gallery-001/dist/gallery-001.rpk`

源 RPK SHA-256：`9ad56f1006804f4eadd41d021dd4ae20f61b0bdf4e6b8b051ab10eb4bae33884`

APK 构建资源：`app/build/generated/host-rpk/gallery-001.rpk`

APK 资源 SHA-256：`9ad56f1006804f4eadd41d021dd4ae20f61b0bdf4e6b8b051ab10eb4bae33884`

RPK 内容：

- `framework/_quickapp-kit_framework-v1.js` 存在；
- `pages/pages/Home/index.js` 声明 `@quickapp-kit/framework-v1` 依赖，并通过 `$app_require$` 使用；
- `pages/pages/Detail/index.js` 声明 `@quickapp-kit/framework-v1` 依赖，并通过 `$app_require$` 使用；
- `app.js`、Home、Detail 和 Framework 均来自该真实 RPK，未使用 Inline 替代输入。

## Native 依赖路径

冷构建生成的 `runtime/.cxx/Debug/*/*/CMakeCache.txt`：

```text
quickapp_runtime_core_SOURCE_DIR=/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-core
quickapp_runtime_js_framework_SOURCE_DIR=/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-js
```

`compile_commands.json` 明确显示：

- Android `runtime_spine.cpp` 使用 `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-core/include` 和 `quickapp-runtime-core/runtime/js/include`；
- Core 源文件来自 `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-core/src/`；
- JS Runtime 源文件和静态库来自 `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-js` 以及 Core 的 `runtime/js`；
- 编译器为 NDK `28.2.13676358`。

## 完整运行日志

- 聚焦 QuickApp 日志：`evidence/f5-shared-framework-android-recheck.log`
- 完整 Android logcat：`evidence/f5-shared-framework-android-recheck-full.log`

关键模块加载日志：

```text
android.js.module id=@quickapp-kit/app kind=app status=loaded error=
android.js.module id=@quickapp-kit/framework-v1 kind=shared status=loaded error=
android.js.module id=@quickapp-kit/page/pages/Home kind=page status=loaded error=
```

关键失败现场：

```text
android.stage=initial.command.js
android.vm.failure phase=onInit error=require outside module evaluation
android.stage=page.vm.failed
android.event.handler_bind surface=srf:1 handler=hdl:1 method=onRefresh bound=0 method_ok=1 vm_ok=0
android.event.handler_bind surface=srf:1 handler=hdl:2 method=onDetail bound=0 method_ok=1 vm_ok=0
```

完整可用的 Runtime 堆栈就是上述 Android/JS 边界日志；没有产生 Java 崩溃或 Native crash，`adb logcat -b crash -d` 为空。当前 Runtime 失败日志没有输出 QuickJS source stack，因此不能伪造更深堆栈。

## 验收矩阵

| 项目 | 结果 | 证据 |
|---|---|---|
| 冷清理 | 通过 | 目录重新生成 |
| 冷构建 | 通过 | Gradle 58 tasks，`BUILD SUCCESSFUL` |
| RPK 源/APK 资源哈希 | 通过 | SHA-256 一致 |
| Framework 文件 | 通过 | RPK 包含目标文件 |
| App 模块加载 | 通过 | `kind=app status=loaded` |
| Shared Framework 加载 | 通过 | `kind=shared status=loaded` |
| Home 页面模块加载 | 通过 | `kind=page status=loaded` |
| Home VM `onInit` | 未通过 | `require outside module evaluation` |
| Home 首屏 Mount | 未执行 | VM 创建失败 |
| 状态更新/RenderTransaction | 未执行 | Handler 未绑定 |
| Event Handler | 未通过 | `vm_ok=0` |
| Detail 模块/页面 | 未执行 | Home VM 初始化失败 |
| push/back 与重复路由 | 未执行 | Home 未建立 |
| teardown | 通过 | `surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0` |

## 与 iOS 路径对照

iOS F5 证据显示同一真实 RPK 的模块顺序为：App `loaded`、Framework `kind=shared loaded`、Home `kind=page loaded`，随后同样在页面 `onInit` 报 `require outside module evaluation`，没有进入 UIKit Mount。

因此本次 Android 与 iOS 的关键差异不是 RPK、Shared Framework 文件或 Native 输入：两端都能加载 Shared Framework，且两端都在页面 VM 的延迟 `$app_require$` 处失败。Android 的冷构建日志进一步确认加载的是本次新编译的 Native binary，而非旧 APK/旧 `.so`。问题位于共享 JS ModuleLoader 对页面 VM 执行期依赖解析的公共语义，不能在 Android 侧修复。

## 修改边界与剩余问题

本次只生成验证日志和本证据文件；没有修改 Core、JS Runtime、Toolkit、公共 Contract、RPK、Router、Runtime Tree、Bridge 或 Android 运行逻辑。

剩余阻塞：Core/JS ModuleLoader 需要支持页面已声明依赖在 `createPageVm` 执行期的合法 `$app_require$`，或调整统一的页面 VM 加载合同。修复完成后应重新执行本复核，再判断 Android F5 Shared Framework 是否完整通过。
