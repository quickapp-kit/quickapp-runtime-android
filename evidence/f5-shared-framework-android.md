# F5 Shared Framework Android 回归

## 结论

F5 Android 回归通过。真实 `gallery-001.rpk` 经共享 Framework、JS Runtime、Core 和 Android Platform Runtime 加载；首屏、增量刷新、详情 `push/back`、重复进入和 teardown 均通过。未修改 Core、JS Runtime、Toolkit、公共 Contract、RPK 或 Router。

## 输入校验

| 项目 | 结果 |
|---|---|
| 源 RPK | `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/gallery-001/dist/gallery-001.rpk` |
| 期望 SHA-256 | `9ad56f1006804f4eadd41d021dd4ae20f61b0bdf4e6b8b051ab10eb4bae33884` |
| 源 RPK SHA-256 | 一致 |
| APK 构建资源 | `app/build/generated/host-rpk/gallery-001.rpk` |
| APK 资源 SHA-256 | 一致 |
| Framework 模块 | `framework/_quickapp-kit_framework-v1.js` 存在 |
| 页面依赖 | Home/Detail `index.js` 通过 `$app_require$` 加载 `@quickapp-kit/framework-v1` |

## 构建与运行

```text
./gradlew :quickapp-host:assembleDebug --rerun-tasks --no-daemon --no-configuration-cache --console=plain
adb install -r app/build/outputs/apk/debug/quickapp-host-debug.apk
adb shell am force-stop dev.quickapp.kit.host
adb shell am start -S -n dev.quickapp.kit.host/.MainActivity
```

APK SHA-256：`360e33394eda4103946d395585a96345a275e781cbb166249d84f84dc4ebfe92`

截图：`/private/tmp/f5-shared-framework-home.png`

## 验收结果

- Host 冷启动后选择 `gallery-001`，真实 Home 首屏显示“设备巡检”、3 条巡检项、图片和“手动刷新”。
- `手动刷新` 触发真实 Click Handler、JS 状态更新和 `RenderTransaction`，列表内容与刷新计数更新。
- 点击真实“查看详情”进入 Detail；Detail 显示图片、标题、状态和“返回任务看板”。
- Detail `back` 返回 Home；随后再次进入和返回，共完成 3 次重复 `push/back`，未发生页面栈或资源累积。
- Home Image 通过 Android Platform Mount 显示；首屏 Mount `operations=132`，Detail Mount `operations=33`。
- 当前最终状态为 Host 列表；Runtime 销毁结果：`surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0`。

## 关键日志

```text
android.stage=rpk.verified
android.stage=js.started
android.js.module id=@quickapp-kit/app kind=app status=loaded
android.js.module id=@quickapp-kit/page/pages/Home kind=page status=loaded
android.stage=page.vm.ready
android.native.mount surface=srf:1 operations=132
android.render.submit surface=srf:1 transaction=txn:srf:1-1 revision=1 ok=1
android.event.handler_execute surface=srf:1 handler=hdl:1 dispatched=1
android.navigation.push request=req:j-100001 source=srf:1 uri=/pages/Detail accepted=1
android.js.module id=@quickapp-kit/page/pages/Detail kind=page status=loaded
android.stage=page.vm.ready
android.navigation.close request=req:j-100002 source=srf:2 accepted=1
android.navigation.close.result request=req:j-100002 source=srf:2 revealed=srf:1 completed=1
android.navigation.push request=req:j-100003 source=srf:1 uri=/pages/Detail accepted=1
android.navigation.close.result request=req:j-100004 source=srf:3 revealed=srf:1 completed=1
android.navigation.push request=req:j-100005 source=srf:1 uri=/pages/Detail accepted=1
android.navigation.close.result request=req:j-100006 source=srf:4 revealed=srf:1 completed=1
android.runtime.destroy.begin surfaces=1 nodes=24
android.runtime.destroy.end surfaces=0 nodes=0
android.runtime.stopped surfaces=0 nodes=0 handlers=0 pendingCallbacks=0 jsResources=0 coreQueue=0 javaSurfaces=0 javaNodes=0
```

## 边界

本次只验证 Android Host 对共享 Framework 的接入。未修改 Core、JS Runtime、Toolkit、公共 Contract、RPK、Router、Runtime Tree 或其他平台；未发现阻塞问题。
