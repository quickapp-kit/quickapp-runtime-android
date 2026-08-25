# B6 URL Provider: Android Status

## Status

`BLOCKED_BY_PUBLIC_CONTRACT`

Android implementation has not started because the required shared input is
not present. The B6 execution plan requires the public implementation agent to
finish `a` lowering, the `system.openUrl`/`system.webview` typed contract, and
the real `url-001.rpk` before platform Provider work begins.

## Verified blockers

- No `url-001.rpk` exists under
  `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples`.
- Core `ModuleId` currently exposes only `system.prompt`, `system.device`,
  `page.host`, `system.fetch`, and `system.file`.
- JS `FeatureModule`/`FeatureMethod` currently encode prompt, fetch, and file
  requests only; there is no `openUrl` or `webview` request ABI.
- Android `AndroidFeatureProvider` therefore has no public typed request it
  could receive for this batch.

## Required handoff input

After the public B6 implementation is complete, provide:

1. The frozen typed module/method contract for external URL and WebView.
2. The generated real `url-001.rpk` and its SHA-256.
3. The manifest capabilities and expected success/failed/closed lifecycle
   result semantics.

Only then can Android implement `Intent.ACTION_VIEW` for external URLs and a
platform-owned Android `WebView` page. Those actions must remain Platform
Provider operations and must not enter Core Router or create an embedded
`webview` Host Component.

No Android source, Core source, JS source, Toolkit source, public Contract, or
example was modified for B6.
