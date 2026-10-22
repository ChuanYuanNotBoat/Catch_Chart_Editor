# CCE 本地审阅、图层与多轮 `.ccepr` 工作流实施计划

## 一、目标与范围

本轮目标是建立一套完整的本地协作审阅流程：

> 本地身份自动署名 → PR 编辑模式制作提案 → 导出 `.ccepr` → 导入审阅 → 评论 / Diff / 冲突处理 → 多轮回传 → 选择性应用 / rebase → 最终保存或导出谱面。

本轮不引入网络账号、实时协作、远程仓库或 Note 永久身份 ID。

### 本轮范围

| 范围 | 包含内容 |
|---|---|
| Note | Normal、Rain、Sound 的新增、删除、显式 Move、属性修改 |
| Timing / BPM List | 条目新增、删除、BPM 值和拍点变化 |
| Metadata | 当前编辑器允许修改的谱面 Metadata |
| Export Metadata | `.mc` 导出时包含哪些 Metadata 字段 |
| 资源 | 音频、背景等编辑流程涉及的资源引用及内容变化 |
| 评论 | 独立评论、回复、引用、解决状态、导航、持久化 |
| 本地身份 | UUID、名称、当前身份、自动署名、自动填写新谱面 creator |
| `.ccepr` | 导出、导入、审阅、冲突、rebase、选择性应用、多轮回传 |
| PR 编辑模式 | Base / Proposal / Diff Overlay、显式 Move 记录 |
| Layer | 绘图软件式内容层、共享、跨难度复用、参考和独立导出 |
| Event Log | 编辑、评论、审阅、rebase、Undo / Redo 等事件记录 |

插件数据及相关 sidecar 暂缓。

`effect`、`extra`、未知扩展等当前未参与编辑的字段不纳入 Diff，也不额外扩展读写。

网络账号、权限系统、实时协作、远程分支管理不作为本轮前置。

---

# 二、核心设计原则

## 2.1 Note 不引入永久 UUID

谱面 Note 不增加持久身份字段。

Normal、Rain、Sound 等 Note 仍由自身数据描述。

普通 Diff 不尝试通过永久身份判断对象连续性。

对于无法可靠判断的对象：

```text
Remove + Add
```

优先于猜测 Move。

---

## 2.2 Review 层对象拥有稳定 UUID

以下对象属于审阅协议层，可以拥有稳定 UUID：

```text
authorId
proposalId
revisionId
changeId
operationGroupId
threadId
commentId
eventId
```

这些 ID 只用于：

- PR 修订关系；
- 评论与回复；
- Change 跟踪；
- 作者关联；
- 多轮导入去重；
- 审阅历史。

它们不能被解释为 Note 的永久身份。

---

## 2.3 自动 Diff 描述结果，PR Editor 记录编辑意图

普通 Diff：

> 比较两个状态之间发生了什么。

PR Editor：

> 记录用户明确进行了什么修改。

因此：

```text
普通比较：
A 消失，B 出现
→ Remove A + Add B
```

而：

```text
PR Editor：
用户直接拖动 A 到 B
→ Move A → B
```

这条原则是 Move 判定的核心。

---

# 三、CCE Document Model 与 `.mc`

`.mc` 后续重构为：

> 导出格式，而不是 CCE 完整内部存储格式。

CCE 内部需要建立独立 Document Model。

概念结构：

```text
CCE Project
├─ Resources
├─ Layers
├─ Difficulties
├─ References
├─ Review Data
└─ Editor State
```

`.mc` 通过 Export Pipeline 从当前 CCE Document 生成。

因此以后明确区分：

```text
Save Project
```

与：

```text
Export Chart / Difficulty / Layer
```

`.mc` 不要求保存：

- Layer 结构；
- Review 历史；
- 评论；
- PR revision；
- UI 状态；
- Reference Layer；
- 其他 CCE 专属信息。

---

# 四、Difficulty 模型

Timing、BPM、音频关联等属于 Difficulty。

概念结构：

```text
Difficulty
├─ Metadata
├─ Timing / BPM List
├─ Offset
├─ Audio reference
├─ oggHash
├─ LayerBindings
└─ Export Configuration
```

Layer 自身不保存：

- BPM；
- Timing；
- offset；
- 音频；
- Difficulty Metadata。

同一个 Layer 可以关联多个 Difficulty。

Note 的拍点保持不变，由所在 Difficulty 的 Timing 解释为实际音频时间。

---

# 五、Layer 系统

Layer 行为采用类似绘图软件的模型。

## 5.1 Layer 是工程级共享内容单元

```text
Project
├─ Layer A
├─ Layer B
└─ Layer C
```

Difficulty 通过 Binding 引用 Layer：

```text
Hard
├─ Layer A
└─ Layer B

Insane
├─ Layer A
└─ Layer C
```

因此：

> Note 严格属于一个 Layer。

但：

> 一个 Layer 可以被多个 Difficulty 正式关联。

---

## 5.2 Layer 本体

Layer 至少包含：

```text
layerId
name
notes
```

Layer：

- 可自定义名称；
- 可以独立导出；
- 不拥有整体 X / 时间变换；
- 不计划支持 Layer Transform。

---

## 5.3 DifficultyLayerBinding

每个 Difficulty 对 Layer 的使用状态独立保存，例如：

```text
difficultyId
layerId

visible
locked
opacity
order
exportEnabled
```

其中：

### `visible`

只决定是否渲染。

隐藏 Layer：

- Note 仍然存在；
- 仍属于 Document；
- 不改变统计语义；
- 不改变导出语义；
- 不改变实际数据。

### `exportEnabled`

独立决定该 Layer 是否参与 `.mc` 导出。

`visible` 与 `exportEnabled` 不能混用。

---

## 5.4 共享 Layer 编辑

如果一个 Layer 同时被多个 Difficulty 正式关联，则允许编辑。

但第一次尝试修改共享 Layer 时应显示明显警告，例如：

> Layer “Drums” 被 3 个难度共享。修改会同时影响这些难度。

提供：

```text
继续编辑共享层
复制为当前难度独立层
取消
```

警告采用编辑会话级确认，不应每次移动一个 Note 都重复弹出。

---

# 六、Reference Layer / Difficulty Reference

Reference 与正式 Layer Binding 必须严格分离。

Reference 表示：

> 只读地把其他 Difficulty / Layer 叠加到当前编辑视图。

Reference 不参与：

- 当前 Difficulty 导出；
- 当前 Difficulty Note 所有权；
- 当前 Difficulty统计；
- 当前编辑操作。

---

## 6.1 Reference 选择范围

可以引用：

```text
整个 Difficulty
```

也可以引用：

```text
单个 Layer
多个 Layer
```

例如：

```text
Reference Hard:
    Layer Drums
    Layer Vocal
```

Reference 使用源 Difficulty 的当前最新内容。

不是创建时 Snapshot。

---

## 6.2 oggHash 约束

只有：

```text
currentDifficulty.oggHash
==
referenceDifficulty.oggHash
```

才能直接进行谱面叠加。

如果 `oggHash` 不同：

> 不允许直接 Overlay。

不得偷偷进行：

- 时间拉伸；
- offset 自动补偿；
- BPM 猜测对齐；
- 音频位置自动映射。

如果以后需要此类功能，应作为独立高级功能设计。

---

## 6.3 共享 Layer 与 Reference 去重

如果某个 Layer 已经正式关联到当前 Difficulty，而某个 Reference Difficulty 又包含同一个 Layer，则同一 `layerId` 不重复绘制两份真实内容。

---

# 七、统一 Render Stack

Layer、Reference、Review Overlay 可以共用底层渲染基础设施，但不能混淆语义。

概念：

```text
Render Stack
├─ Current Difficulty Layers
├─ Live References
├─ PR Base Snapshot
├─ Diff Ghosts
├─ Move Preview
├─ Selection / Tool Preview
└─ Comment Anchors
```

可以共享：

- visibility；
- opacity；
- drawing order；
- hit-test infrastructure；
- viewport transform。

但：

```text
Chart Layer
Reference Layer
Review Overlay
```

仍然是不同的数据类型。

---

# 八、PR Editor Mode

PR Editor Mode 用于制作和修改 `.ccepr` 提案。

存在两个基本状态：

```text
Base
Proposal
```

其中：

### Base

- PR 创建时的冻结 Snapshot；
- 永远只读；
- 不允许直接修改；
- 更换 Base 必须通过显式 Rebase。

### Proposal

- 当前可编辑提案；
- 可以正常编辑；
- 本身必须保持为完整合法谱面；
- 可以另存为普通 Difficulty。

将 Proposal 保存成普通谱面时：

> 不携带 PR 历史、评论、Change ID 等 Review 数据。

---

# 九、PR Editor 视图模式

PR Editor 至少支持三种组合行为：

### Proposal Only

完全隐藏 Base。

表现基本等同普通编辑器。

### Diff Overlay

显示 Proposal 与 Base 差异。

### Base View

直接切换到只读 Base。

另外支持：

### Peek Base

按住快捷键临时显示 Base，松开后回到 Proposal。

切换时尽量保持：

```text
zoom
scroll
beat position
viewport
```

Base View 必须有明显：

```text
BASE · READ ONLY
```

之类视觉提示。

---

# 十、Move 判定

## 10.1 PR Editor 中直接拖动

如果用户在 PR Editor 中拖动一个 Base 原本存在的 Note：

```text
旧位置 → 新位置
```

自动记录为：

```text
Move
```

因为编辑操作本身提供了明确对应关系。

---

## 10.2 删除后重建

如果用户：

```text
删除 A
然后新建 B
```

默认必须解释为：

```text
Remove A
Add B
```

不能自动推断 Move。

只有用户显式执行：

```text
Link as Move
```

才将两者关联成 Move。

---

## 10.3 普通状态 Diff

对两个普通谱面状态进行事后比较时：

只有关系完全确定时才允许识别 Move。

存在歧义时：

```text
Remove + Add
```

优先。

---

# 十一、多选移动

多个 Note 一次性移动时：

底层仍生成多个独立 Change：

```text
Move Note A
Move Note B
Move Note C
```

并共享：

```text
operationGroupId
```

这样可以：

- 一次 Undo；
- 作为一次用户操作显示；
- 整体审阅；
- 必要时拆分接受。

---

## 11.1 Diff 渲染

默认不要给每个 Note 画一根长连接线。

多选移动优先显示：

```text
旧 Selection Bounds
        ↓
新 Selection Bounds
```

即：

> 一个旧多选框 → 一个新多选框。

选中、悬停或展开时，可以进一步显示单 Note 对应关系。

---

# 十二、EditableChartSnapshot

需要建立统一可编辑快照：

```text
EditableChartSnapshot
```

只包含本轮参与 Diff 的数据：

- Normal Note；
- Rain；
- Sound Note；
- Timing / BPM；
- Metadata；
- Export Metadata Configuration；
- 资源引用；
- Layer / Difficulty 相关必要状态。

明确排除：

- 运行期 Note ID；
- 机器绝对路径；
- QWidget 状态；
- 临时缓存；
- renderer 内部对象。

---

# 十三、DocumentVersion

跨进程和跨机器审阅不能依赖：

```text
ChartController::revision()
```

进程内 revision 继续用于：

- 后台任务失效；
- 防止过期结果提交。

跨文件审阅新增：

```text
DocumentVersion
```

包含：

- 文档关联信息；
- Canonical Snapshot Hash；
- 内容版本；
- 必要资源 Hash。

---

# 十四、ChangeSet

ChangeSet 描述 Proposal 相对于 Base 的修改。

基础类型包括：

```text
Add
Remove
Move
Modify
```

也允许：

```text
Move + Modify
```

内部更适合统一表示成：

```text
UpdateNote
{
    before
    after
    changedFields
    moveIntent
}
```

UI 再决定如何表达。

---

## 14.1 changeId

每个 Change 可以拥有稳定：

```text
changeId
```

它表示：

> 这项提案修改。

不是：

> 被修改 Note 的身份。

如果后续 revision 继续修改同一项提案，可以沿用 `changeId`。

---

# 十五、Note 属性 Inspector

补充现有一直缺失的 Note 属性小面板。

建议放在谱面统计区域上方或附近。

普通编辑模式：

> 显示当前选中 Note 的属性。

多选模式：

> 显示共同属性以及 Mixed 状态。

PR Diff 模式：

> 自动切换为 Before / After 属性对照。

例如：

| 属性 | Base | Proposal |
|---|---|---|
| Beat | `128:0/4` | `128:1/4` |
| X | `0.32` | `0.47` |
| Width | `0.10` | `0.10` |

只变化字段突出显示。

Rain、Sound 使用各自实际字段，不强行套用 Normal Note 的属性模型。

---

# 十六、Timing / BPM Diff

Timing 属于 Difficulty。

BPM List 独立比较：

- 条目新增；
- 条目删除；
- BPM 修改；
- Beat 修改。

相同拍点的重复条目必须按数量正确处理。

BPM 修改导致的：

```text
音频时间变化
```

不能被误报成：

```text
Note Beat Move
```

拍点比较使用精确有理数。

写回时保留原始三元组表达能力。

---

# 十七、Metadata 与 Export Metadata 分离

Meta 页面必须视觉和数据模型上分离：

## Chart Metadata

真正属于谱面 / Difficulty 的 Metadata。

例如：

```text
Title
Artist
Creator
Difficulty Name
Level
...
```

---

## Export Metadata Configuration

控制 `.mc` 导出时：

> 哪些字段被包含。

例如：

```text
✓ Title
✓ Artist
✓ Creator
✗ Album
```

不要把：

```text
字段值
```

和：

```text
是否导出
```

混成同一个模型。

因此 Diff 中也分别显示：

```text
Metadata Change
```

和：

```text
Export Configuration Change
```

---

# 十八、本地身份

本轮不是完整账号系统。

其定位仅为：

> 本地署名 Profile / Identity。

功能只有：

- PR 自动署名；
- 评论自动署名；
- Event Log 自动署名；
- 新建谱面时自动填写 `creator`。

---

## 18.1 `.cceidentity`

创建身份时立即生成稳定 UUID。

例如：

```json
{
  "version": 1,
  "identities": [
    {
      "authorId": "UUID",
      "authorName": "Mystia"
    }
  ],
  "activeAuthorId": "UUID"
}
```

规则：

- `authorId` 创建后不改变；
- 改名只修改 `authorName`；
- 删除身份后 UUID 不复用；
- 新建身份永远生成新 UUID；
- 不承担认证和安全含义。

---

## 18.2 作者快照

评论、PR、Event 记录：

```text
authorId
authorNameSnapshot
```

之后本地身份改名：

> 历史内容继续显示创建时名称。

---

## 18.3 creator

新建谱面时：

```text
creator = activeIdentity.authorName
```

UUID 不写入 `.mc`。

---

# 十九、评论系统

Comment Thread 与 Comment Entry 都拥有稳定 UUID。

```text
threadId
commentId
```

删除之后 UUID 仍然保留于历史中，不能重新使用。

---

# 二十、评论 Anchor

评论支持：

| Anchor | 行为 |
|---|---|
| TimePoint | 绑定空拍点 / 时间 |
| TimePosition | 绑定时间 + 空间位置 |
| Region | 绑定时间 / 空间区域 |
| Note | 一个 Note |
| NoteGroup | 多个 Note |
| BpmEntry | 单个 BPM 条目 |
| BpmList | 整个 BPM List |

评论不要求必须绑定已有对象。

因此可以自然表达：

> “这里应该补一个 Note。”

---

# 二十一、评论悬浮窗

评论卡片采用：

```text
Anchor
+
DisplayOffset
```

两者严格分离。

拖动评论框：

> 只改变显示位置。

不会修改评论 Anchor。

拖动距离存在最大限制，避免评论框离 Anchor 过远。

---

## 21.1 显示状态

评论至少支持本地：

```text
Expanded
Collapsed
Hidden
```

这些属于 UI 本地状态，不随 `.ccepr` 传播。

而：

```text
Resolved
Unresolved
```

属于共享审阅状态。

---

# 二十二、Note Anchor 跟随

如果评论绑定一个 Note：

### Note 移动

评论跟随移动。

### Note 删除

评论不删除。

评论保留在：

> 最后一次可靠定位位置。

并显示：

```text
绑定的 Note 已删除
```

Anchor 状态进入：

```text
Missing
```

如果后续 Undo、rebase 或重新匹配恢复目标，则允许重新变为：

```text
Resolved / Valid
```

---

# 二十三、多 Note 评论

NoteGroup 是一等 Anchor 类型。

一个评论可以连多个 Note。

如果：

```text
3 个目标中 1 个删除
```

仍保留剩余绑定。

UI 可以显示：

```text
2 / 3 targets available
```

所有目标失效后，Comment 仍不删除。

---

# 二十四、PR Diff 中评论连接

普通编辑视图：

> 评论跟随当前 Note。

PR Diff View：

如果一个绑定 Note 存在可靠：

```text
before
after
```

映射，则评论连接线同时连接：

```text
修改前 Note
修改后 Note
```

因此可以直观看到：

> 评论针对的是一个被移动 / 修改的对象。

多 Note 评论同理。

---

# 二十五、评论编辑权限

双击编辑评论时：

```text
comment.authorId
==
activeAuthorId
```

才允许修改正文或删除。

导入其他人的 PR：

> 不能修改对方评论。

可以：

- 回复；
- 引用；
- 导航；
- 根据审阅规则进行 Resolve / Reopen。

该规则只是 UI / 协作语义。

不是安全认证。

---

# 二十六、评论删除与历史

评论删除不等于历史中彻底消失。

通过：

```text
Deleted state
```

或 Event Log Tombstone 保留其身份。

因此旧 revision 回传时：

> 可以识别这是对已删除评论的旧操作。

而不是生成幽灵新评论。

---

# 二十七、ProposalRevision

`.ccepr` 使用稳定：

```text
proposalId
```

每次逻辑修改生成新的：

```text
revisionId
```

Revision 使用：

```text
parentRevisionIds[]
```

允许形成 DAG。

例如：

```text
      R1
     /  \
   R2A  R2B
     \  /
      R3
```

以支持：

> 两个人从同一个旧 `.ccepr` 分别修改后再次交换。

---

# 二十八、Revision 内容

每个 ProposalRevision 应能独立重建当前逻辑状态。

包括：

- Base 信息；
- 当前 Proposal ChangeSet；
- 当前评论；
- 当前回复；
- 审阅决策；
- Event Log；
- 父 Revision；
- 作者；
- 资源引用；
- 必要资源。

资源本身允许哈希去重，不要求每个 revision 物理复制所有文件。

---

# 二十九、`.ccepr` 包结构

首版建议：

> JSON Manifest + 结构化数据 + 独立资源区域。

至少包含：

```text
formatVersion
proposalId
revisionId
parentRevisionIds
author
description

baseDocument
baseSnapshotHash

changes
comments
reviewDecisions
events

resources
```

必要的新 / 修改资源直接包含。

未变化资源允许通过 Hash 引用。

具体容器格式在 P0 冻结。

---

# 三十、PR 多轮回传

支持：

```text
创建 Proposal
→ 导出
→ 审阅
→ 评论 / 回复
→ 回传
→ 修改 Proposal
→ 处理冲突
→ 再次导出
→ 再次审阅
→ Apply
```

再次导入：

- 根据 `proposalId` 识别已有 PR；
- 根据 UUID 去重；
- 同 revision 重复导入保持幂等；
- 分叉 revision 根据共同祖先合并；
- 不依赖递增版本号。

---

# 三十一、Review Decision 与 Apply State 分离

至少区分：

## Review Decision

```text
Pending
Accepted
Rejected
```

## Apply State

```text
NotApplied
Applied
AlreadySatisfied
Conflict
```

## Anchor State

```text
Valid
Modified
Missing
Ambiguous
```

这些不能混成一个枚举。

因为：

> 接受修改 ≠ 已经写入谱面。

Undo、Rebase、目标谱面变化之后：

> Apply State 必须重新校验。

---

# 三十二、三方合并

使用：

```text
Base
Current
Proposal
```

三方比较。

默认：

| 情况 | 处理 |
|---|---|
| Current == Base | 可以应用 Proposal |
| Current == Proposal | AlreadySatisfied |
| 修改不同字段 | 校验后合并 |
| 修改不同对象 | 校验后合并 |
| 同一属性双方结果不同 | Conflict |
| 对象匹配不明确 | Ambiguous |
| 存在依赖 | Group Review |

---

# 三十三、依赖组

以下变化需要显式依赖关系：

- 音频；
- 主音轨 Sound Note；
- offset；
- BPM；
- 相关 Note；
- 必须共同应用的资源修改。

UI 可以整组处理。

---

# 三十四、Rebase

Rebase 是显式操作。

只有 Rebase 才能：

> 更换 PR Base。

Rebase 必须：

- 保留旧 revision；
- 创建新 revision；
- 重新计算 ChangeSet；
- 重新校验 Review Decision；
- 重新定位 Comment Anchor；
- 产生 Event。

---

# 三十五、Diff UI

审阅界面至少包括：

```text
Note Diff
Timing / BPM Diff
Metadata Diff
Export Settings Diff
Resource Diff
Comment
Conflict
```

建议颜色：

```text
绿色：新增
红色：删除
黄色：冲突
```

Move：

> 显示 Before / After。

Timing：

> 使用专门表达，不强行套 Note Move UI。

---

# 三十六、Diff 导航

至少支持：

```text
Previous / Next Comment
Previous / Next Change
Previous / Next Conflict
```

并允许筛选：

- Add；
- Remove；
- Move；
- Modify；
- Comment；
- Unresolved；
- Conflict。

---

# 三十七、统一事务应用

真正 Apply 时使用统一服务。

流程：

1. 重新校验谱面 revision；
2. 检查旧值；
3. 检查资源状态；
4. 校验所有依赖；
5. 准备资源；
6. 一次提交 Note / Timing / Metadata / Export Config / Resource；
7. 更新 Review State；
8. 发布一次完整分类通知。

任一步失败：

> 全部回滚。

一次批量 Apply：

> 一个 Undo 项。

---

# 三十八、资源原子性

需要区分：

## 运行时原子性

操作失败时恢复：

- 内存；
-资源；
- Review 状态。

## 持久化原子性

程序中途崩溃后：

> 不允许工程落入半新半旧状态。

P0 / P5 需要确定：

- 临时文件；
- staged write；
- commit manifest；
- crash recovery。

---

# 三十九、Undo / Redo

Undo / Redo 必须同步恢复：

- 谱面数据；
- 资源引用；
- Review Apply 状态；
- Event Log 对应状态。

不能出现：

```text
谱面已经 Undo
但 PR 仍显示 Applied
```

---

# 四十、Event Log

Event Log 与 Comment 正文分离。

记录例如：

```text
CreateComment
EditComment
DeleteComment
ResolveComment
ReopenComment

CreateChange
AcceptChange
RejectChange
ApplyChange

Rebase
ImportRevision
MergeRevision

Undo
Redo
```

每个 Event 有稳定：

```text
eventId
```

再次导入时按 ID 去重。

---

# 四十一、实施阶段

| 阶段 | 内容 | 完成门槛 |
|---|---|---|
| **P0：契约与模型** | 冻结 Document、Difficulty、Layer、Snapshot、ChangeSet、Identity、Comment、Revision、Export 契约 | 数据模型无明显冲突 |
| **P1：Document / Diff Core** | Editable Snapshot、Canonical Hash、Note / Timing / Metadata Diff | 确定性、重复项正确、无 Note UUID |
| **P2：Layer 基础** | Layer、Binding、共享、Reference、oggHash 检查、Layer Export | 多难度共享和参考工作正确 |
| **P3：Local Identity + Comment** | `.cceidentity`、署名、Anchor、评论 UI、导航、持久化 | 重启不丢、删除 Note 不丢评论 |
| **P4：PR Editor Mode** | Base / Proposal、Move 意图、多选 Move、Diff Overlay、Inspector | 可以完整制作 Proposal |
| **P5：`.ccepr` Roundtrip** | 导出、导入、revision DAG、评论与 Event 合并 | 多轮回传、重复导入幂等 |
| **P6：Merge / Rebase / Transaction** | 三方比较、Conflict、Apply、Rollback、Undo | 混合修改原子应用 |
| **P7：Review UI** | 完整 Diff、筛选、导航、属性 Diff、资源 Diff | 全流程无需手工改文件 |
| **P8：回归与交付** | 性能、失败恢复、翻译、帮助、TODO、文档 | 达到最终验收 |

---

# 四十二、第一个可体验节点

第一个真正可体验的节点建议为：

> **P3：Layer + 本地身份 + 评论闭环。**

此时用户已经可以：

- 使用 Layer；
- 跨 Difficulty Reference；
- 创建身份；
- 自动署名；
- 在谱面上添加评论；
- 评论跟随 Note；
- 保存和恢复。

---

# 四十三、完整功能节点

最终目标：

> **P8：完整离线多轮 PR 审阅系统。**

支持：

```text
制作 Proposal
→ 导出 .ccepr
→ 导入
→ Diff
→ 评论
→ 回复
→ 回传
→ 分叉
→ 合并
→ Rebase
→ 选择性接受
→ Apply
→ Undo / Redo
→ 导出最终 .mc
```

---

# 四十四、验收场景

## Diff

- Normal；
- Rain；
- Sound；
- 重复 Note；
- Remove + Add；
- PR Editor Move；
- 手动 Link as Move；
- 多选 Move；
- Note 属性修改；
- 等价拍点。

## Timing

- BPM List Add / Remove；
- BPM 值变化；
- Beat 变化；
- 重复 BPM Entry；
- BPM 修改不误报 Note Move。

## Layer

- 一个 Note 只属于一个 Layer；
- Layer 跨 Difficulty 共享；
- 共享编辑警告；
- Copy as Independent Layer；
- Layer 独立导出；
- visibility 仅控制显示；
- exportEnabled 独立；
- 单 / 多 Layer Reference；
- 整个 Difficulty Reference；
- oggHash 不同禁止 Overlay。

## Metadata

- Chart Metadata 修改；
- Export Metadata Configuration 修改；
- 两者 Diff 独立显示；
- creator 自动填写。

## Comment

- TimePoint；
- TimePosition；
- Region；
- 单 Note；
- 多 Note；
- BpmEntry；
- BpmList；
- Note Move 跟随；
- Note 删除保留；
- Before / After 双连接；
- 双击编辑；
- UUID 不匹配不可编辑；
- Collapse / Hide；
- Resolve / Reopen。

## PR

- 原样重导；
- 更新后回传；
- 旧包重复导入；
- 并行 revision；
- DAG Merge；
- 评论并行修改；
- 已删除评论旧回复；
- 部分 Apply 后继续更新。

## Conflict / Rebase

- Base 改变；
- Current 删除对象；
- Change 目标歧义；
- 同属性不同修改；
- Resource Conflict；
- Rebase 后评论重新定位。

## Transaction

- 损坏 `.ccepr`；
- 缺失资源；
- Hash 不一致；
- Apply 中途失败；
- 资源写入失败；
- Crash Recovery；
- Undo / Redo。

## Performance

至少使用：

```text
5k Notes
20k Notes
100k Notes
```

验证：

- Diff 不进行全量 O(n²) 匹配；
- 大谱面 Overlay 响应；
- 评论列表；
- 多 Layer Render Stack；
- Reference；
- 后台结果过期；
- Inspector；
- 导航。

---

# 四十五、当前已经冻结的关键结论

1. `.mc` 逐步转为 Export Format。
2. Note 不增加永久 UUID。
3. Review 层对象可以使用 UUID。
4. PR Editor 的直接拖动明确记录 Move。
5. 删除后新建不自动视为 Move。
6. Base 永远只读。
7. Proposal 可以另存为普通 Difficulty。
8. Layer 是工程级共享对象。
9. Note 严格属于一个 Layer。
10. Layer 可以关联多个 Difficulty。
11. 编辑共享 Layer 前显示明显警告。
12. Layer 无整体时间 / X Transform。
13. Layer 可自定义名称。
14. Layer 可独立导出。
15. Reference 可选择整个 Difficulty、单个 Layer 或多个 Layer。
16. 普通 Reference 使用源 Difficulty 当前最新内容。
17. `oggHash` 不同禁止直接 Overlay。
18. Timing / BPM 属于 Difficulty。
19. Metadata 与 Export Metadata Configuration 分离。
20. 本地身份只负责署名和 creator 自动填写。
21. Identity 创建时立即分配 UUID。
22. 评论可以绑定空时间、空间位置、区域、单 / 多 Note、BPM。
23. 评论框可有限范围拖动，但 Anchor 不变。
24. Note 移动时评论跟随。
25. Note 删除时评论保留并提示。
26. PR Diff 中评论可同时连接 Before / After。
27. 评论只有作者 UUID 匹配时才允许编辑。
28. 评论删除后 ID 仍进入历史。
29. ProposalRevision 使用 DAG，而非单纯递增版本号。
30. Diff Overlay、Layer 与 Reference 共用渲染基础设施，但保持独立语义。