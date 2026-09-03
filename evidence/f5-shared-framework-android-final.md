# F5 Shared Framework Android Final Regression

## 结论

Android Host 使用目标 SHA-256 的真实 Gallery RPK 完成最终回归：Shared Framework、App/Page VM、首屏、状态更新、点击、Detail push/back、重复进入 3 次和 teardown 全部通过；最终日志没有 `require outside module evaluation`。

本次未修改 Core、JS Runtime、Toolkit、公共 Contract、RPK、Router、Runtime Tree、Bridge 或 Android 运行逻辑。

## 目录

- [测试输入](#测试输入)
- [构建与安装](#构建与安装)
- [包与依赖校验](#包与依赖校验)
- [运行结果](#运行结果)
- [剩余问题](#剩余问题)

## 测试输入

用户指定路径：

`quickapp-examples/showcases/gallery-001/dist/gallery-001.rpk`

当前工作区的实际文件路径为：

`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/inspection-board/dist/gallery-001.rpk`

该实际文件 SHA-256 为：

`e35b477b237dd846e2a419b8ee7d02e3b9a2b9cac1348ee1e70fa50480c9b52c`

Android 构建生成包：

`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/app/build/generated/host-rpk/gallery-001.rpk`

生成包 SHA-256 同为：

`e35b477b237dd846e2a419b8ee7d02e3b9a2b9cac1348ee1e70fa50480c9b52c`

## 构建与安装

冷清理范围：Android `.gradle`、根 `build`、`app/build`、`runtime/build`、`app/.cxx`、`runtime/.cxx`。未删除源码、RPK 或公共依赖。

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android
./gradlew :quickapp-host:assembleDebug --no-daemon --no-configuration-cache --console=plain
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am force-stop dev.quickapp.kit.host
adb shell am start -S -n dev.quickapp.kit.host/.MainActivity
```

构建结果：`BUILD SUCCESSFUL in 52s`，`58 actionable tasks: 58 executed`。

APK SHA-256：

`426f2ff468c1a1fc7039de66b083b8a032df27fe05a0022ae9aaeac57826d208`

APK 包含 `assets/gallery-001.rpk`、`lib/arm64-v8a/libquickapp_android_runtime.so` 和 `lib/x86_64/libquickapp_android_runtime.so`。

## 包与依赖校验

RPK 包含：

- `app.js`
- `framework/_quickapp-kit_framework-v1.js`
- `pages/pages/Home/index.js`
- `pages/pages/Detail/index.js`
- Home/Detail Page IR
- `quickapp-kit/runtime.json`

`runtime.json` 声明 `@quickapp-kit/framework-v1` 为 shared module，Home 和 Detail 均依赖该模块。最终进程日志明确记录：

```text
android.js.module id=@quickapp-kit/app kind=app status=loaded error=
android.js.module id=@quickapp-kit/framework-v1 kind=shared status=loaded error=
android.js.module id=@quickapp-kit/page/pages/Home kind=page status=loaded error=
android.js.module id=@quickapp-kit/page/pages/Detail kind=page status=loaded error=
```

Fresh CMake 生成配置实际引用：

```text
quickapp-runtime-core=/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-core
quickapp-runtime-js=/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-js
NDK=28.2.13676358
```

## 运行结果

完整原始日志：

`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android/evidence/f5-shared-framework-android-final.log`

验证链路：

| 阶段 | 结果 | 证据 |
| --- | --- | --- |
| RPK 校验与 Runtime 启动 | 通过 | `android.stage=rpk.verified`、`android.stage=js.started` |
| App/Shared Framework/Home 加载 | 通过 | 三个模块均 `status=loaded` |
| App/Page VM 初始化与首屏 | 通过 | `android.stage=page.vm.ready`、Home `operations=132`、`mount.result ok=true` |
| 状态更新 | 通过 | click handler、`RenderTransaction revision=1`、增量 mount `operations=9` |
| Detail push/back 第 1 次 | 通过 | `req:j-100001` / `req:j-100002` |
| Detail push/back 第 2 次 | 通过 | `req:j-100003` / `req:j-100004` |
| Detail push/back 第 3 次 | 通过 | `req:j-100005` / `req:j-100006` |
| teardown | 通过 | `surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0` |

关键 teardown 日志：

```text
android.runtime.destroy.begin surfaces=4 nodes=43
android.runtime.destroy.end surfaces=0 nodes=0
android.runtime.stopped surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0
```

最终 Host 进程仍在运行，焦点回到 `dev.quickapp.kit.host/.MainActivity`，说明退出 RPK 后没有退出 Host。

## 剩余问题

- 本次 Android 目标已通过；未发现 `require outside module evaluation`。
- 用户指定的 `showcases/gallery-001` 路径在当前工作区不存在，构建配置使用内容相同且哈希匹配的 `showcases/inspection-board/dist/gallery-001.rpk`；若需要路径语义统一，应由 Examples/发布目录另行处理，本次不改动。
- iOS 最终回归需由 iOS Platform Agent 在其 Simulator 上独立完成并写入对应 evidence。
