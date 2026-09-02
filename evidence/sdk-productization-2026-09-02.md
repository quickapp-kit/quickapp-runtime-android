# Android SDK Productization Evidence

日期：2026-09-02

## 结论

Android Runtime 已拆分为 `quickapp-runtime-android` Library 和 `quickapp-host` APK。
Library 生成不携带 RPK 的 AAR，并包含 JNI shared library；Host 只负责本地 RPK
列表、受控私有目录复制、View 容器和 Runtime 生命周期。

产品化构建通过。指定的真实 `commerce-001.rpk` 当前不在工作区，因此商品案例的
完整首屏、Tabs、列表和详情验收未宣称通过。

## 交付物

- AAR：`runtime/build/outputs/aar/quickapp-runtime-android-release.aar`
- AAR SHA-256：`418ad469a950f85ed2491511b1beff5f615109b6b83ed8338a6e893426b5ed37`
- Host APK：`app/build/outputs/apk/debug/quickapp-host-debug.apk`
- Host APK SHA-256：`28247e39f9ce910119f3892e51570789dfa2fa9b5afd58b9ff3734cbf8c52168`
- AAR ABI：`arm64-v8a`、`x86_64`
- AAR 内容：`classes.jar`、`libquickapp_android_runtime.so`、`libc++_shared.so`
- AAR 内容检查：无 `assets/` 和 `.rpk` 文件

## 公共 Facade

`dev.quickapp.kit.android.QuickAppRuntime` 暴露：

- `create(Context)`
- `loadRpk(File)`
- `attachSurface(ViewGroup)`
- `dispatchInput(QuickAppInput)`
- `updateLifecycle(QuickAppLifecycleState)`
- `destroy()`

Facade 不暴露 Core 指针、Runtime Tree、NodeId 或 NativeHandle。销毁后输入被拒绝，
重复 `destroy()` 返回 `COMPLETED`。当前 Runtime Spine 尚未暴露前后台控制，
`updateLifecycle` 对该能力返回 typed `UNSUPPORTED`，没有伪造成功。

## 边界与安全

- Host 只选择并复制 RPK 到应用私有目录，再调用 AAR。
- AAR Java 层执行 ZIP、大小、成员路径和原生文件后缀预校验。
- C++ PackageLoader 继续执行权威包格式、资源和 checksum 校验。
- JNI 只转换 typed 值、回调和不透明 Runtime 句柄。
- Android View 操作通过主线程 Handler 执行；Core/JS 保持既有 owner thread。
- Host 不创建第二套路由、第二棵 Tree、旁路 Bridge 或业务状态。

## 构建验证

```text
./gradlew :quickapp-runtime-android:buildCMakeDebug --no-daemon --console=plain
BUILD SUCCESSFUL

./gradlew :quickapp-runtime-android:bundleReleaseAar --no-daemon --no-configuration-cache --console=plain
BUILD SUCCESSFUL

./gradlew :quickapp-host:assembleDebug --no-daemon --no-configuration-cache --console=plain
BUILD SUCCESSFUL

./gradlew :quickapp-runtime-android:lintDebug :quickapp-host:lintDebug --no-daemon --no-configuration-cache --console=plain
BUILD SUCCESSFUL
```

模拟器：`emulator-5554`。安装命令：

```text
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am start -n dev.quickapp.kit.host/.MainActivity
```

Host 已在模拟器启动；当前 Catalog 收录 24 个真实 RPK，支持滚动浏览和点击加载。
目录截图：`/private/tmp/quickapp-host-catalog-all.png`、
`/private/tmp/quickapp-host-catalog-all-scroll.png`。

使用已存在的 `gallery-001.rpk` 进行 AAR 烟测时，日志通过了：

```text
android.stage=rpk.verified
android.stage=js.started
android.js.module id=@quickapp-kit/app kind=app status=loaded
android.js.module id=@quickapp-kit/page/pages/Home kind=page status=loaded
android.native.mount surface=srf:1 operations=132
android.platform.mount.result surface=srf:1 ok=true
android.initial.result surface=srf:1 prepared=1
```

## 阻塞项

指定输入：

`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`

当前不存在，无法计算本次指定输入的 SHA-256，也无法完成 commerce 真实验收。
工作区中没有找到同名 RPK；旧的 `app/build/generated/case001-assets/commerce-001.rpk`
属于历史生成缓存，未用于本次验收。

因此以下项目保持 `BLOCKED/UNVERIFIED`：commerce 首屏、Image/Text/Button、
List/Scroll、Tabs、if、状态更新、push/back、重复加载和 teardown 资源归零。

## 修改范围

- Android Gradle 多模块：`runtime` Library、`app` Host。
- Java/Kotlin Facade、JNI 可见内部类、Host Activity、Manifest 和构建说明。
- 未修改 Core、JS Runtime、Toolkit、公共 Contract、RPK 或其他平台。
