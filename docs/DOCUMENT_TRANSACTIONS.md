# 活动谱面的只读版本与增量事务

当前实现位于 `src/document/ChartSnapshot.h`、`ChartDelta.{h,cpp}` 和
`src/controller/ChartController.{h,cpp}`，使用 QtCore/C++17，不依赖新的账号或工程模式。
旧 `.mc` 编辑、保存、快捷键及插件入口继续可用，文件格式没有增加 Note ID。

## 读取与提交

同步 UI 使用 `ChartController::chart()` 的只读指针；后台任务捕获 `snapshot()`。
快照共享 Note/BPM 容器，后续写入触发分离，快照不会随实时文档变化。
`ChartReadVersion` 区分控制器与加载会话，并按依赖域记录 revision；它只属于运行期。

生成整谱候选的服务可用以下流程提交：

```cpp
const auto base = controller.snapshot();
Chart candidate = base.chart();
// 在 candidate 上生成修改，不访问实时谱面的可变数据。
CceDocument::ChartDelta delta;
if (CceDocument::ChartDelta::between(base.chart(), candidate, delta, &error))
    controller.applyTransaction(actionName, base.version(), delta, &error);
```

`applyTransaction` 要求完整来源/版本仍然有效，验证增量和编辑保护，并在保护回调返回后
再次核对版本。失败不推入历史；成功只提交一个命令、发布一次类型化 revision。
读取当前版本不能为旧候选补做可信的基准校验，调用方必须保留生成候选时的版本。

## 增量与逆操作

`ChartDelta` 不保存两张 Chart，而是保存：

- Note 序列的变更片段：原/目标位置、修改前/后数据。
- 发生变化时的 BPM 列表；保留原始有理拍点、顺序和重复项。
- 发生变化时的完整可编辑 MetaData；包括音频/背景引用、offset、速度等。
- 发生变化时的本地音频源路径。

未修改的 Note 数据不进入镜像；BPM 未变化时不保留它的列表。Note 匹配先跳过公共前后缀，再对候选相同项的出现次数
计算递增子序列，保留未改内容作为锚点。远距离移动不会把中间整段 Note 存入历史。
生成最坏为 O(n log n)，应用 Note 数据增量为一次线性重建；只有元数据变化且句柄未重建时继续共享 Note 容器。

未修改内容按数据和出现位置匹配，工程重新解码并重建所有句柄时也不会留下整谱数据增量。
句柄重建单独保存为运行期位置/句柄映射，不保存未修改 Note 的数据镜像，也不进入序列化数据。
因此导入后即使每个句柄都变了，少量编辑的可序列化记录仍然很小；会话内的精确 Undo 则保留必要的句柄映射。
恢复这些句柄可能使运行期 Note 容器分离，但不会被序列化成谱面内容修改。
重复或空句柄同样可以记录和还原，不要求用户修复旧数据才能使用原有编辑入口。
Undo 由所存增量生成逆操作，精确恢复原始顺序、拍点表示、已有运行期句柄及模型字段。

`ExternalMutationCommand`、`DocumentMutationCommand` 及旧批量插件的兼容分支均使用增量，
不再保留整张 Chart 的前后副本。普通添加/移动/删除和 timing-only 命令继续使用已有紧凑操作。
原有不透明插件输入的数量上限保留；`kMaxOpaqueSnapshotNotes` 名称仅为源码兼容，不代表仍有整谱历史回退。

## 数据校验与失败行为

应用前检查修改前数据、Note 数量/位置、BPM、元数据及源路径的相关前置条件。
所有结果先在临时 Chart 中构造，全部成功后才替换输出；失败时输出与辅助工程状态均不变。
Controller 在移动 Undo/Redo 游标前再次预检，拒绝不匹配的历史操作并保留历史与 revision。
辅助工程状态仍在谱面发布前恢复，既有评论/审阅与一步撤销的顺序保持不变。

Core 层的数据前置条件只约束本增量触及的数据；Controller 的完整 `ChartReadVersion`
负责拒绝未触及领域也发生变化的过期候选。Core 数据片段本身不充当完整版本信封。

## 本地序列化 v1

`ChartDelta::toJson/fromJson` 提供 `schemaVersion: 1` 的严格结构：

- `beforeNoteCount` / `afterNoteCount`、`notes` 与 `handleLinks`。
- 每个片段包含 `beforeIndex` / `afterIndex` 与修改前后 Note 数据。
- 可选的 `timing`、`metadata`、`audioSource` 替换记录。

`handleLinks` 只表示同一次事务中原位置与目标位置之间的关系，不保存 UUID。
导入时 `bindTo` 根据原位置和完整数据绑定当前会话句柄，随后保留实际逆操作供 Undo 使用。
仅句柄变化不构成序列化的 Note 修改，回放沿用当前会话的句柄。可空字符串用 JSON `null`
区分普通空字符串，避免往返时丢失模型状态。
未知字段、额外 Note ID、未知版本、不合法目标、交叠片段、非法引用和错误数量会被拒绝，
解析失败不会覆盖已有增量。

这是本地命令数据格式，可能包含本机资源路径，不是 `.ccepr` 或 `.mc` 格式。
它没有自动写入谱面、工程或提案包，也没有引入新文件后缀或强制迁移。
分享提案继续使用原有 `.ccepr` 的资源封装与审阅校验。

## 仍待完成的边界

- 工程/图层的通用事务与逆操作；Workspace 的辅助 `ProjectBundle` 仍保留状态快照。
- 将已有原生命令统一接入可序列化 journal、持久基准/revision 信封和恢复回放。
- 工程级只读 `ComposedChartView`、分层索引、Timeline 和持久实体强类型 ID。
- 完整 Workspace 长历史的实际内存与交互基准。

`ChartDeltaTests` 的 5k/20k/100k 连续移动检查的是 ChartDelta 保留的数据量，
不能用它替代完整 Workspace 历史内存验收。当前范围与后续任务见
[ENGINEERING_TODO.md](ENGINEERING_TODO.md)，验证记录见 [TESTING.md](../TESTING.md)。
