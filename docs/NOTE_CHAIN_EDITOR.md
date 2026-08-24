# 原生 Note Chain 曲线编辑器

> 适用版本：Beta v1.11.0 开发周期
> 权威实现：`src/editor/NoteChain/`
> 最后核对：2026-08-24

## 定位

Note Chain 是主程序内部的 C++ 曲线编辑器。用户在谱面画布上编辑锚点、控制柄和曲线段，再把目标段按指定密度提交为真实 Catch 普通音符。

它不再依赖 `builtin.note_chain_assist` Python 外部进程。旧 Python sidecar 仅作为向后兼容输入；现行交互和持久化语义以 C++ 实现与自动化测试为准。

## 模块结构

| 文件 | 职责 |
|------|------|
| `NoteChainCommon.h` | chart-space 数据结构、选择/拖拽状态、持久化扩展元数据和常量 |
| `NoteChainCurveSampler.h` | 贝塞尔/折线采样、按 beat 归一化及指定 beat 的 lane 求值 |
| `NoteChainState.h/.cpp` | 锚点、连接、段密度/形状、选择、分组、快照和缓存版本 |
| `NoteChainPersistence.h/.cpp` | V3 sidecar 序列化、兼容读取、CAS 校验和原子保存 |
| `NoteChainEditor.h/.cpp` | 输入分发、绘制、命令、提交音符、样式导入导出和宿主撤销协作 |
| `ChartCanvasNoteChain.cpp` | `ChartCanvas` 与编辑器之间的投影、输入、右键菜单及重绘接线 |

## 坐标与数据约束

- 持久状态始终使用 **chart-space**：`laneX` 范围为 `0..512`，纵轴为浮点 beat。
- 鼠标命中半径、框选矩形和拖拽预览使用 canvas 像素。
- `CanvasProjection` 是 chart-space 与 canvas 像素之间唯一允许的交互转换入口。
- 控制柄在状态中保存为相对锚点的 lane/beat 偏移；不得把像素偏移写入 sidecar。
- 连接键使用排序后的 `(minAnchorId, maxAnchorId)`，避免同一连接出现双向重复键。
- 提交曲线时只生成普通 Note；Rain/Sound 不属于曲线生成目标。

## 主要交互

- `Curve`、`Plugins -> Curve Edit Tool` 或 `Place Anchor` 启用工具。
- 锚点放置开启时，左键空白处按 beat 顺序插入锚点并生成默认控制柄。
- 左键拖动锚点或控制柄；双击锚点切换平滑/折角。
- `Shift + 拖动锚点`、`Connect Selected` 和 `Disconnect Selected` 管理连接。
- 选择目标可独立启用 Anchors、Segments 和 Notes；空白拖拽执行框选。
- 段形状为 `curve` 或 `polyline`。
- 段密度为 Follow Editor（跟随当前 Time Division）或固定分母。
- `Enter` 提交整条曲线；段右键菜单可只提交上下文命中的目标段。
- `Snap Notes to Curve` 在普通音符拖拽或粘贴预览时按 beat 计算曲线 laneX。

完整用户操作见 [help.md](help.md#原生曲线工具note-chain-assist)。

## V3 sidecar

默认路径：

```text
chart.mc
.mcce-plugin/chart.curve_tbd.json
```

V3 数据覆盖：

- `nodes`：稳定节点身份、chart-space 拍点/位置、控制柄、平滑状态和分组；
- `curves`：稳定 `curve_id`、唯一 `curve_no`、端点、形状、密度模式/分母和分组；
- `node_groups` / `curve_groups`：分组定义；
- 根级元数据：格式版本、文件 UUID、revision、writer 和更新时间；
- 未识别或保留字段：读取后原样带回，避免 C++ 编辑破坏 Python 时代的扩展数据。

兼容读取支持旧 `anchors`、handle 和 `links` 表达。兼容逻辑只能影响反序列化入口；保存后应输出规范 V3 结构。

## 保存安全

- 保存使用 `QSaveFile` 原子提交，不先删除已有 sidecar。
- 根级 `revision` 用于 CAS：内存状态的 revision 与磁盘不一致时拒绝覆盖。
- 加载先解析到临时状态，全部验证成功后才替换当前项目。
- 切换谱面失败时保留原项目状态、当前路径和后续保存目标。
- sidecar 跟随工作副本、保存、复制、难度切换和 MCZ 相关文件同步流程。

## 撤销与宿主协作

- 编辑器内部快照上限由 `Const::kMaxHistory` 控制，当前为 128。
- 有效变更才写入历史；拖拽在手势结束时提交一次最终状态。
- 通过 `requestHostUndoCheckpoint` 把曲线编辑接入主程序统一撤销时间线。
- `onHostUndo` / `onHostRedo` 根据曲线 checkpoint 恢复内部快照。
- 曲线提交为音符时经 `ChartController` 批量修改，必须保持单个可撤销动作。

## 绘制与性能约束

- 曲线由 C++ 直接使用 `QPainter` 绘制，不序列化为插件 overlay。
- 鼠标移动按 16ms 节流，但 press/release 和最终提交不得丢失。
- 每段采样结果由曲线 revision 驱动缓存，绘制和命中检测复用同一采样结果。
- 绘制前按扩展 viewport 裁剪不可见锚点/曲线；状态变化必须使曲线缓存失效。
- 不在输入处理或绘制路径中调用嵌套 `processEvents()`、同步外部进程或磁盘保存。

## 自动化覆盖

`tests/minimal_tests.cpp` 当前覆盖：

- revision 增长及过期 CAS 写入拒绝；
- Python legacy 锚点/控制柄读取；
- 锚点排序插入和默认控制柄；
- triplet 分母保留；
- 非单调贝塞尔采样归一化；
- V3 元数据、分组、密度和保留字段往返；
- 非法 curve identity 回退；
- 损坏 payload 不替换状态；
- 项目切换事务性；
- 宿主 Note 选择与最近锚点同步。

修改数据结构、采样、保存或宿主接线时，至少运行：

```powershell
cmake --build build --config Debug --target CatchChartEditorTests --parallel
ctest --test-dir build -C Debug -R core_minimal_tests --output-on-failure
```
