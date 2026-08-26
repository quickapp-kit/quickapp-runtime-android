# Android Event Payload Validation

日期：2026-08-26

## 结论

Android Event Payload 丢失问题已修复。Core `RuntimeValue::Object` 现在在 Android
适配边界递归转换为 JS ABI 的 typed `RuntimeValue`，再写入
`JsEventDispatch.typed.payload`；没有转成字符串、通用 JSON 或平台私有状态。

Tabs 的 `0 -> 1 -> 2 -> 3 -> 0` 已全部产生正确 payload、JS Handler、RenderTransaction
和内容切换。其他控件 payload 也已验证。Scroll 的 payload 类型正确，但高频 scroll
回调会暴露已有的事务节流/生命周期边界，详见下文。

## 修改

- 文件：`quickapp-runtime-android/src/runtime_spine.cpp`
- 新增 Core RuntimeValue 到 JS RuntimeValue 的递归 typed conversion。
- `JsCoreIngress::post(JsEventDispatch&&)` 使用转换后的 payload 构造 JS ABI dispatch。
- 增加 Android 结构化 payload 类型诊断日志；不改变事件语义和队列语义。
- 未修改 Core、JS、Toolkit、RPK、公共 Contract 或 Examples DSL。

## 构建

```text
./gradlew :app:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

构建结果：`BUILD SUCCESSFUL`。

- RPK：`quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk`
- RPK SHA-256：`d0317e888354356a965c1eb7e8b07aa17fbe9aad99c447223b348607c5b780d4`
- 生成 Android 资产 SHA-256：同上
- APK SHA-256：`50b0571367615b8ccc64ecc0c57b2d7d06c66671ef050e1815484f317d21a71e`

## Payload 验证

| 事件 | 结果 | 证据 |
|---|---|---|
| Tabs change | `index=number`, `value=string`；四次切换均有 RenderTransaction | `commerce-001-payload-tabs.log`、`commerce-001-payload-tabs-0..3.png` |
| Input focus/input/change | `value=string` | `controls-001-payload.log`、`controls-001-payload.png` |
| Switch change | `checked=boolean` | `controls-001-payload.log` |
| Slider change | `value=number`, `isFromUser=boolean` | `controls-002-payload-slider.log`、`controls-002-payload-slider.png` |
| Picker change | `selected=number`, `value=string` | `controls-002-payload-picker.log`、`controls-002-payload-picker.png` |
| Scroll/scrollbottom | `scrollOffset=number`，并带 `contentSize`、`viewportSize` | `capability-gallery-scroll-payload.log`、`capability-gallery-scroll-payload.png` |
| Click | 空 payload，`keys=0` | `controls-001-payload.log`、`commerce-001-payload-route.log` |

Tabs 日志确认：

```text
0 -> 1 -> 2 -> 3 -> 0
index=number value=string
revision=1 -> 2 -> 3 -> 4, ok=1
```

Scroll 日志确认前 7 次事务成功，之后高频回调出现既有的
`LIFECYCLE_BUSY` / `ABI_INVALID_ARGUMENT`。payload 仍持续为
`scrollOffset=number`；本任务不修改 Core 以绕过该问题。

## 路由与 teardown

真实 `commerce-001.rpk` 回归通过：

```text
Home -> Detail -> Home
Home -> Detail -> Home
```

两次 push/back 均通过 Core Navigation；第二次 Detail 使用新 Surface，未出现旁路路由
或重复业务状态。证据：`commerce-001-payload-route.log`、
`commerce-001-payload-detail-a.png`、`commerce-001-payload-home-after-repeat-back.png`。

Activity 退出触发：

```text
android.runtime.destroy.begin surfaces=3 nodes=172
```

进程随后退出，但本次未观测到 `android.runtime.destroy.end` 或零资源终态日志，因此不将
完整 teardown 资源归零标记为本次已验证通过；这是独立的 Android teardown 观测缺口。
