# Shop RPK Path Migration Android Verification

## 结论

路径迁移后的 Android Host 可以加载并渲染真实 `shop.rpk`。Loader、Home 首屏、图片、20 条商品列表、详情 `push/back` 和 teardown 均通过。当前唯一阻塞是 RPK 中的 `<tabs>` 未进入 Android 可交互视图树，因此 Tabs 切换未通过验收。

## 输入与哈希

- RPK：`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/shop/dist/shop.rpk`
- RPK SHA-256：`f4a21abc7480a1d96134c4f7fdaf5a4f45ae745ad26457b689ebf7b9fd67d719`
- 生成资产：`app/build/generated/host-rpk/shop.rpk`
- 生成资产 SHA-256：`f4a21abc7480a1d96134c4f7fdaf5a4f45ae745ad26457b689ebf7b9fd67d719`
- APK：`app/build/outputs/apk/debug/quickapp-host-debug.apk`
- APK SHA-256：`26b544246f83fd7745cc0bd98bf8abea7af8afdc861d6480dbd0c95412535ea8`

RPK 包含 `META-INF/runtime.json`、`pages/`、`shared/` 和 `assets/`，包括 Home、ProductDetail、Shared Framework、图片资源和视频资源。

## 构建与运行

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-android
rm -rf .gradle build app/build runtime/build app/.cxx runtime/.cxx
./gradlew :quickapp-host:assembleDebug --no-daemon --no-configuration-cache --console=plain
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am start -S -n dev.quickapp.kit.host/.MainActivity
```

构建成功；安装成功；Host 从本地 RPK 目录打开 `shop.rpk`。

## 验收结果

| 项目 | 结果 |
| --- | --- |
| `rpk.verified` | 通过 |
| App/Shared Framework/Home 加载 | 通过 |
| Home 首屏与图片 | 通过 |
| 20 条商品列表与滚动 | 通过 |
| 商品详情 push | 通过，`/pages/ProductDetail` |
| 商品详情 back | 通过，恢复 `srf:1` |
| Tabs | 阻塞：视图树无“首页/种草/购物车/我的”可交互节点 |
| RPK 退出 | 通过 |
| Runtime teardown | 通过：`surfaces=0 nodes=0 handlers=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0` |
| Host 保持运行 | 通过 |

关键日志：

```text
android.stage=rpk.verified
android.js.module id=@quickapp-kit/app kind=app status=loaded error=
android.js.module id=@quickapp-kit/framework-v1 kind=shared status=loaded error=
android.js.module id=@quickapp-kit/page/pages/Home kind=page status=loaded error=
android.navigation.push request=req:j-100001 source=srf:1 uri=/pages/ProductDetail accepted=1
android.navigation.close.result request=req:j-100002 source=srf:2 revealed=srf:1 completed=1
android.runtime.stopped surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0
```

## 边界结论

本次未修改 Core、JS、Toolkit、公共 Contract、RPK、Router 或平台语义。当前不是旧 APK、旧 RPK 或 Loader 路径问题；Tabs 缺失应交给 Android Platform Component 映射继续定位。
