# Async Overlay 异步化 — 状态报告

## 分支: `temp/async-overlay-wip`

基于 `v2-main`，包含 10 文件 / +303 -89 行变更。

---

## 已完成的架构变更

### 1. ExternalProcessPlugin 异步化
- **QObject 多重继承**: `class ExternalProcessPlugin : public QObject, public PluginInterface`
- **`canvasOverlays()` 重写**: 缓存命中→返回；pending→返回旧缓存；否则发异步请求+立即返回
- **`sendAsyncOverlayRequest()`**: 写 stdin 后立即返回，不等待响应
- **`onProcessReadyRead()`** (槽): 一次性连接，读取 stdout 中的 overlay 响应
- **`tryProcessOverlayResponse()`**: 解析、缓存、emit `canvasOverlaysUpdated` 信号
- **`drainProcessResponseLine()`**: 在 `requestJson`/`probeProcessHealth` 同步循环中拦截 overlay 响应
- **`m_deferredResponseBuffer`**: 缓存在同步循环中无法立即处理的响应，退出循环后 drain

### 2. PluginManager 信号转发与聚合
- 新增信号 `canvasOverlaysUpdated(QList<CanvasOverlayItem>)`
- 新增私有槽 `onPluginOverlayUpdated`: 收到插件 overlay 后重建聚合并发射信号
- `m_pluginOverlayCache`: 按 pluginId 缓存各插件 overlay，用于聚合重建
- `canvasOverlays()`: 内部记录各插件缓存
- `initializePendingPlugins()`: 连接 ExternalProcessPlugin 的信号

### 3. ChartCanvas 改造
- **`triggerOverlayRefresh()`**: 调用 `pm->canvasOverlays()`（非阻塞），更新缓存和重绘
- **`onCanvasOverlaysUpdated()`**: 接收聚合信号，直接更新缓存
- **`setPluginToolMode(true)`**: 连接 PluginManager 信号 + 异步刷新 + 启动定时器
- **`onOverlayQueryTimerFire()`**: 简化，仅调用 `triggerOverlayRefresh()`
- **防抖**: `wheelEvent`/`setScrollPos` 滚动停止 150ms 后才发异步刷新
- **`setTimeScale`/`resizeEvent`/`handleCanvasInput`**: 立即刷新或更新缓存

---

## 已修复的回归问题（3/4）

| # | 问题 | 根因 | 修复 |
|---|------|------|------|
| 1 | 鼠标移动卡顿 | `invalidateCanvasOverlayCache()` 重置 `m_overlayRequestPending`，导致每次 handleCanvasInput 取消进行中的异步请求 + 触发重复请求 | 修改 `invalidateCanvasOverlayCache()` 仅清缓存，不清 pending 标志 |
| 2 | 锚点不随谱面滚动 | `setPluginToolMode(true)` 忘了调用 `startOverlayQueryTimer()`，定时器从未启动 | 添加 `startOverlayQueryTimer()` |
| 3 | 锚点放置后不显示 | 同上 + `beatOffset` 在 `drawPluginOverlays` 中与 `beatToY()` 重复补偿 | 移除 `beatOffset`，`beatToY()` 已正确处理绝对 beat 滚动 |
| 4 | MetaEditPanel.refreshMeta 不必要触发 | 待确认（可能是 chartChanged 信号传播） | 待分析 |

---

## 仍需处理的项

1. **问题 4**: MetaEditPanel 过度刷新 — 需在 `v2-main` 上定位触发链
2. **`handleCanvasInput` 同步 IPC**: 鼠标事件仍走 `requestJson`(150ms 超时)，非本次改造范围
3. **完整功能验证**: 需要实际运行编辑器，开启曲线插件，测试播放/滚动/拖拽/缩放
4. **`invalidateCanvasOverlayCache()` 语义变化**: 不重置 pending 可能使缓存"假失效"（等待中的请求返回后会覆盖），需确认无副作用

---

## 回滚方案

```bash
git checkout v2-main   # 回到稳定分支，所有异步 overlay 变更在 temp/async-overlay-wip
```

## 继续开发

```bash
git checkout temp/async-overlay-wip   # 切换到异步 overlay 工作分支
```
