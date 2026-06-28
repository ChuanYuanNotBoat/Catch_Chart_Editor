# 插件系统 Bug 追踪与修复 TODO

> 创建日期：2026-06-28 | 排查范围：ExternalProcessPlugin IPC、PluginManager、ChartCanvas overlay 系统

---

## Bug #1：曲线插件叠加层闪烁 ✏️ 已修复

**现象**：曲线绘制工具的叠加层出现新旧状态交替闪烁（非持久出现）。

**根因**：`ExternalProcessPlugin` 内部维护独立的 overlay 缓存（`m_cachedCanvasOverlays`），用于加速定时器查询。当用户交互触发 `handleCanvasInput()` 后，该内部缓存未同步更新，导致 33ms 定时器用旧缓存覆盖画布最新状态。

**修复**：在 `ExternalProcessPlugin::handleCanvasInput()` 中解析 overlay 响应时同步更新内部缓存。

**修改文件**：`src/plugin/ExternalProcessPlugin.cpp:475-480`

---

## Bug #2：Shift 连接节点卡顿闪退 ✏️ 已修复

**现象**：按住 Shift 拖动连接节点时程序卡顿后闪退。

**根因**：两个因素叠加：
1. `handleCanvasInput`（50ms 阻塞 IPC）与 `onOverlayQueryTimerFire`（33ms 定时器）在事件循环中**重入** — mouseMove 事件洪水期间，定时器在 `waitForReadyRead` 处理事件时触发，导致两层阻塞嵌套
2. pipe buffer 死锁 — Python stdout 写满后阻塞，C++ 端同时等待响应，形成死锁导致闪退

**修复**：在 `dispatchPluginCanvasInput` 期间加 `m_overlayQueryInCanvasInput` 标志位阻止 overlay 查询定时器重入。同时添加 mouseMove 16ms 节流防止事件洪水。

**修改文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvas.h` + 成员、`ChartCanvas.cpp` 构造+检查、`ChartCanvasInteraction.cpp` 设标、`ChartCanvasMouse.cpp` 节流

---

## 🔴 Critical 级问题

### C1 — `mutable QProcess m_process` 设计违规
- **文件**：`src/plugin/ExternalProcessPlugin.h:80`
- **描述**：`m_process` 声明为 `mutable` 仅为绕过 `const` 方法中修改进程状态。`canvasOverlays()` 标记 `const` 却会写 stdin/重启进程，破坏 const 语义和线程安全分析。
- **修复方向**：去除 `const` 或将 IPC 通道抽离为非 const 内部对象。

### C2 — `requestJson` 阻塞主线程（7 个调用点）
- **文件**：`src/plugin/ExternalProcessPlugin.cpp` — `canvasOverlays`、`handleCanvasInput`、`toolActions`、`runToolAction`、`buildToolActionBatchEdit`、`openAdvancedColorEditor`、`panelWorkspaceConfig`
- **描述**：所有对 Python 进程的请求均同步阻塞主线程（30ms~15000ms）。`handleCanvasInput` 在 mouseMove 高频触发时导致 UI 冻结。
- **修复方向**：改为异步请求/回调模式（架构级改进，工作量大）。

### C3 — mouseMove 事件无节流 ✏️ 已修复
- **文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvasMouse.cpp:1140`
- **描述**：`setMouseTracking(true)` + 每帧直接调用阻塞 IPC。60+ events/sec × 50ms timeout 致命。
- **修复**：添加 16ms 节流（`kPluginMouseMoveMinIntervalMs`），丢弃过度密集事件。

### C4 — `const_cast` + `forceRestartProcess` 悬空引用 ✏️ 已修复
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:549`
- **描述**：`requestJson()` 超时/writeLine 失败时调用 `forceRestartProcess` 重建 `m_process`。但调用方栈上仍持有 `m_process` 的引用，restart 后引用悬空导致 UB。
- **修复**：用 `m_pendingProcessRestart` 标志替代 inline 重启。延迟到下次 `ensureProcessRunning()` 入口执行，避免调用方仍持有引用时重建。

---

## 🟠 High 级问题

### H1 — `initializePendingPlugins` 无互斥保护
- **文件**：`src/plugin/PluginManager.cpp:685-742`
- **描述**：异步初始化追加 `m_plugins` 的同时，通知回调在迭代同一列表，且无互斥。可能导致看到不完整插件列表。
- **修复方向**：添加互斥锁或将初始化改为同步完成后再触发回调。

### H2 — `canvasOverlays` 忽略 context
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:407`
- **描述**：`Q_UNUSED(context)` — 发送空 payload。滚动/缩放变化不会触发缓存失效，overlay 在画布变换后位置错误。
- **修复方向**：将 context 序列化传入请求 payload，或至少用 scrollBeat/timeScale 哈希做缓存键。

### H3 — overlay 查询结果无条件覆盖（与 Bug #2 相关） ✏️ 已修复
- **文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvas.cpp:672-678`
- **描述**：`m_overlayCache = newItems` 在定时器回调中无条件执行。定时器重入时，刚由 `handleCanvasInput` 设置的正确 overlay 被定时器结果覆盖。
- **修复**：用 `m_overlayQueryInCanvasInput` 标志位防止重入覆盖。

### H4 — 空 overlay 覆盖缓存 ✏️ 已修复
- **文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvasInteraction.cpp:666`
- **描述**：`handleCanvasInput` 返回空 overlay 时（如 key 事件），直接覆盖 `m_overlayCache`，导致曲线短暂消失。
- **修复**：添加 `!result.overlay.isEmpty()` 检查，仅在返回非空 overlay 时更新缓存。

### H5 — `catch(...)` 吞掉异常
- **文件**：`src/plugin/PluginManager.cpp:474`
- **描述**：插件迭代中 `canvasOverlays()` 抛异常只打日志继续，但已损坏的迭代器/状态可能导致不可预测行为。
- **修复方向**：异常后跳过当前插件，重置迭代状态。

### H6 — 超时后静默失败
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:578-589`
- **描述**：`requestJson` 超时后调用 `probeProcessHealth` + `forceRestartProcess`，但调用方继续执行并使用已设为空的 `*result`。关键通知（如 `onChartLoaded`）静默丢失。
- **修复方向**：超时后设置错误码/异常，让调用方可感知和处理。

---

## 🟡 Medium 级问题

### M1 — 缓存失败结果永久化
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:414-423`
- **描述**：`canvasOverlays()` 请求失败后设置 `m_canvasOverlaysCached = true` 并返回空列表。除非外部显式调用 `invalidateCanvasOverlayCache()`，永不再重试。
- **修复方向**：失败时不设置缓存状态，保留下次重试机会。

### M2 — canvasOverlays 缓存不按 context 失效
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:410`
- **描述**：缓存仅在 `forceRestartProcess` 时失效。滚动、缩放、谱面变化都不会触发失效。
- **修复方向**：监听 `onChartChanged` 等通知，及时失效缓存。

### M3 — `triggerPluginToolAction` 双重阻塞
- **文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvasInteraction.cpp:747`
- **描述**：`runToolAction`（15s 超时）后立即同步 `canvasOverlays`（30ms 超时），阻塞主线程两次。
- **修复方向**：合并为一次异步请求。

### M4 — `floatingPanels()` 每次重建
- **文件**：`src/plugin/PluginManager.cpp:385-446`
- **描述**：每次调用遍历所有插件并创建新 widget，无缓存，频繁调用致内存碎片。
- **修复方向**：添加缓存，仅在面板配置变化时重建。

### M5 — `drawPluginOverlays` 无坐标缓存
- **文件**：`src/ui/CustomWidgets/ChartCanvas/ChartCanvasRender.cpp:432-510`
- **描述**：每次 paintEvent 重新计算 chart→canvas 坐标映射，对大量 overlay item 有性能损耗。
- **修复方向**：预计算坐标，在 context 不变时复用。

---

## 🟢 Low 级问题

### L1 — 超时常量硬编码
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:56-73`
- **描述**：`listCanvasOverlays=30ms` / `handleCanvasInput=50ms` 固定值，Python 负载高时不够灵活。
- **修复方向**：改为可配置或自适应超时。

### L2 — 残留数据丢弃
- **文件**：`src/plugin/ExternalProcessPlugin.cpp:785-786`
- **描述**：进程重启时读取残留 stdout/stderr 直接丢弃，丢失诊断信息。
- **修复方向**：日志输出残留数据。

---

## 修复优先级

| 优先级 | 修复项 | 状态 |
|--------|--------|------|
| **P0** | C4: const_cast+forceRestartProcess 悬空引用 | ✅ 已修复 |
| **P0** | Bug #2 / H3: overlay 定时器重入保护 | ✅ 已修复 |
| **P1** | C3: mouseMove 事件节流 | ✅ 已修复 |
| **P1** | H4: 空 overlay 不覆盖缓存 | ✅ 已修复 |
| **P1** | H2: canvasOverlays 传入 context | ⬜ 待修复 |
| **P2** | M1/M2: 缓存失效策略完善 | ⬜ 待修复 |
| **P2** | C2: 改为异步 IPC | ⬜ 待修复 |
| **P3** | C1: 去除 mutable QProcess | ⬜ 待修复 |
| **P3** | H1/H5/H6/M3-M5/L1-L2: 其余改进 | ⬜ 待修复 |

---

## 已修复记录

| 日期 | Bug | 修改文件 | 提交 |
|------|-----|---------|------|
| 2026-06-28 | #1 叠加层闪烁 | `src/plugin/ExternalProcessPlugin.cpp` | `13034fb` |
| 2026-06-28 | #2 Shift 卡顿闪退 + H3 重入保护 | `ChartCanvas.h/.cpp/.Interaction.cpp` | `82a60f0` |
| 2026-06-28 | C4 悬空引用 | `ExternalProcessPlugin.h/.cpp` | `2d24888` |
| 2026-06-28 | C3 mouseMove 节流 + H4 空 overlay | `ChartCanvas.h/.cpp/Mouse.cpp/Interaction.cpp` | `4917061` |
