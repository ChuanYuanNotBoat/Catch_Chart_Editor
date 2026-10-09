# AE / AC 三层界面、详细 Debug GUI 与 AE 修复计划

规划：2026-10-07；实施：2026-10-08，Asia/Singapore。状态：本轮八项宿主 TODO 已完成，完成审计、原生构建及定向验收通过。第 2–9 节保留规划时的设计基线，当前落地状态见第 10 节。

AE 指 Analysis Editor；AC 指 AutoTimingCore。三层已由用户确认：**简洁操作 / 高级分析 / 详细 Debug（Normal / Advanced / Debug）**。用户在本轮补充了此前整理的 AE 已知问题，以下将它们与当前源码和本地接续状态逐项核对。

## 1. 本轮范围与事实来源

目标是让普通使用、深入分析、算法调试各有合适的入口，并修复 AE 的结果状态、选区和交互问题。主要地基是统一 musical-time transform、稳定配置/Profile、纯数据分析轨道和 AE 职责拆分。算法结果来自 AC，CCE 负责输入、bridge、显示、定位、预览和显式应用。

用户本轮补充的 AE 已知问题清单是历史需求的直接来源；其中“仅在开发分支”“快捷键仍全局迁移中”“corpus manifest 未建立”等时点描述，应以第 2 节的当前核对为准。

已读取的跨对话资料：

- 《设计 Analysis Editor》：工具返回了近期实施、移交消息，没有返回早期布局和 bug 讨论。
- 《根据download下的交接包接手。同时注意pull后的本地仓库情况。分支前缀不要用codex用feat之类的性质前缀》：已核对接续状态及停止测试、收尾要求。
- 《BPM编辑工具设计》《Timing编辑工具方案》：Start 用 Malody beat 三元组并吸附；End 用自由音频时间；默认 1 beat，支持分数；手动插值需要控制内部误差及累计漂移。
- 《结合记忆解读分析》：频谱、瞬态和数值图是分析主体，文字用于解释。
- 《谱面审阅功能计划群》《寻找简单功能修复》：分析结果使用纯数据与非破坏性预览；AE 保持独立窗口；多文档适配属于后续工作。

本地主要来源：

| 来源 | 用途 |
| --- | --- |
| [AE 实现说明](ANALYSIS_EDITOR.md) | 已有布局、手动测量、插值、Transient 和诊断能力 |
| 原验收计划：`artifacts/autotiming-handoff-20261007/plan/AUTOTIMING-Implementation-Acceptance-Plan.md` §6 | 四组 Debug tab 的内容和语义 |
| 移交说明：`artifacts/autotiming-handoff-20261007/CODEX-HANDOFF.md` | AC / CCE 分工及历史修复范围 |
| 本地收尾记录：`artifacts/autotiming-handoff-20261007/local-results/TAKEOVER-STATUS.md` | 当前 AC 修改、已完成验证、失败和停止测试状态 |
| [工程 TODO](ENGINEERING_TODO.md) | CCE 集成与基础设施约束 |

`artifacts/` 中的移交包和验证记录是本机资料，不随 Git 仓库分发；其路径以上述当前工作区为根。

实施不运行 AutoTiming 算法、实曲准确度或上游冒烟测试，不修改 Core 算法或生产 gitlink，不自动提交或推送。采用宿主 GUI、配置、投影、撤销及人工数据夹具验证；历史算法记录只作为已有证据使用。

## 2. 当前实现基线

规划时基线：CCE 为 `v2-main` / `b97a1e6`，规划前工作区干净。实施在 `feat/ae-analysis-workbench`，基准提交仍为 `b97a1e6`；生产 AC 子模块仍是 `e47016d`。独立候选工作树在 `artifacts/autotiming-core-candidate-20261007`，分支 `fix/tempo-family-curve-accuracy-20261007`，七个文件已暂存，未纳入本轮修改。

已有能力应直接保留和复用：

- 独立 AE 窗口，共享 ChartController、播放时钟、选区、loop 和 undo。
- 竖向音频时间、左右声道频谱、紧凑 Note/Rain lane、可隐藏和调整的侧栏。
- Timing 内的 Measure / AutoTiming / Interpolate，以及 Transient 曲线和峰值列表。
- candidates、windows、window candidates、track、segments、anchor fit、BPM approximation、raw periodicity、rhythm 表格及 JSON 导出。
- 诊断行定位到音频；候选恒定 BPM 网格预览；source generation、取消和旧回调拒绝的基础保护。
- 手动测量及插值的预览、显式应用、一次撤销和精确 beat 三元组保护。

主要缺口是 musical-time-first 的独立视图、稳定配置与 Debug 参数分层、完整 Profile、分析轨道、曲线比较、bridge 解释字段、全曲 overview，以及 AE 尚未暴露的 local/global 选项。

历史描述的状态修正：

- AE 已包含在当前 `v2-main` / `b97a1e6`，不再是“只活在开发分支”；这不等于完整 AE 已完成。
- `ENGINEERING_TODO.md` 的统一键盘快捷键事项已有完成记录。AE 的 Undo/Redo/播放已走自己的 CommandRouter，但当前测试明确防止主窗口 Delete 泄漏进 AE；后续应补 AE 自己的上下文命令，不能直接打开全部主窗口空间命令。
- 已有 32 份音频、39 个别名的 corpus manifest 及 baseline/candidate 全曲记录；歌曲独立标注及七份精确上游资源仍缺失。无需重建同一 corpus 或重新启动已停止的测试。
- 手动插值已有解析内部误差、自适应细分、实际 chart cache drift 和精确 triplet 检查。剩余工作是统一 proposal 层及必要的新行为验证，不重写已有积分/插值算法。

## 2.1 Musical time / Audio time 的统一边界

编辑和谱面语义以 beat / subdivision 为权威；DSP、解码和实测事件保留 sample / audio time 为权威。两者通过版本化 transform 连接，不能把原始音频 evidence 强行变成依赖当前谱面 BPM 的数据。

拟建立一个共享 `MusicalTimeTransform`，复用 `BeatPosition`、`MathUtils` BPM cache 及既有连续 phase model：

| 对象 | 权威坐标 | 转换规则 |
| --- | --- | --- |
| Note/Rain、snap、Start beat、谱面 timing | 原始 Malody beat 三元组 / 精确有理 beat | beat→audio 是投影；禁止用显示 ms 重建并覆盖原 triplet |
| PCM、频谱帧、onset、End 实测值、AC pulse | sample 或绝对音频时间 | 显示 beat 是带 transform ID 的派生坐标；offset 不写入原音频位置 |
| 现有 chart timing | 离散 BPM map + offset | 共享正反变换，覆盖阶跃和非整数 beat 边界 |
| AC 或手动 proposal | 已发布的连续/分段 tempo/phase map | 有独立模型原点、区间及有效性，不自动替换 chart 的 snap reference |

Normal/Advanced 默认提供 musical-time view，以 beat/subdivision 表达纵向范围和 zoom；absolute-time view 保留供音频测量及 Debug 检查。两种视图都保持时间纵向，切换时保持当前音频游标和可见中心。主编辑器同步视图继续沿用同一 chart reference。

`ms / px` 的旧设置迁移到 absolute-time view；musical zoom 另存 beat 范围/比例。频谱仍按 sample 做 STFT，显示时按 transform 分段映射帧，BPM change 或连续曲线下不能继续用单一均匀 ms 图片拉伸冒充 beat 对齐。

无明显前奏节拍、gap 和无证据区间可以保持已有/可靠 tempo 的延续，同时标 uncertainty。**无 onset ≠ BPM=0，也不自动重启 phase。** 首个可靠模型之前的 beat 原点不得伪装成已识别 downbeat。Tempo-only 候选不能制造 pulse；未覆盖模型区间不提供无依据的精确反变换。

transform cache 与 timing revision 绑定；viewport、snap、候选解释和 overlay 使用明确的 reference。切换所看 hypothesis 只改变分析 overlay，不能悄悄改变 Note 编辑坐标。

## 3. 三层界面与参数边界

三层是同一 AE 的信息和配置层级，共享分析 session、结果和选中对象；切换层级不会重新分析，也不会隐式 reset 配置。默认 Normal；Advanced 为稳定用户功能；Debug 明确显示版本绑定的内部参数。

| 层级 | 主要用途 | 默认内容 | 展开后内容 |
| --- | --- | --- | --- |
| 简洁操作 | 定位、测量、判断是否有可用建议 | 频谱、Note/Rain、播放、选区；手动 Measure / Interpolate；AutoTiming 范围、启动、取消、简短结果与不确定原因 | 进入分析详情、候选预览、应用预览 |
| 高级分析 | 理解时间、速度与音频证据的关系 | tempo/phase 曲线、可信覆盖、候选与替代解释、Transient、选区联动 | 局部证据偏好、复杂分度选项、窗口与候选详情 |
| 详细 Debug | 解释 AC 为什么选择、拒绝或失败 | 四组 Debug tab、原始字段、模式/版本/输入身份、参考与失败记录 | 完整 JSON、本地比较记录导入/导出、详细模型和证据字段 |

布局与操作约束：

1. 保持 AE 原有竖向时间轴；数值曲线可采用横向数值、竖向音频时间，和频谱共享时间范围、翻转及播放指示。
2. 核心工作区保持 Spectrum 左、Note/Rain lane 右；侧栏可折叠，Advanced/Debug 不复制主编辑器的可任意漂移 dock 布局。默认宽度策略延续紧凑约半窗的体验；把 spare QWidget 占位替换为明确的工作区宽度策略，并统一最小/最大宽度及持久化。
3. 常用层不铺满原始字段。confidence 显示“证据较充分 / 有歧义 / 证据不足”及具体来源，例如覆盖不足、倍频分支冲突、相位不足；不发明概率阈值。Advanced 展开解释和关键值，Debug 保留原值、gate 与原因。
4. 点击曲线、窗口或失败区间，更新同一个选中对象并定位音频；只有明确的播放动作才开始播放。
5. 候选网格、所选 tempo model、已有谱面 timing、手动测量、手动插值分别提供图例与开关。
6. 切层级、展开面板、选择候选和导出均不改谱；Apply 是独立动作，沿用现有确认、revision 检查和一次 undo。
7. 结果缺失或尚未实现时显示 unavailable / 未实现及原因；不得补 0 或给出假识别结论。

### 3.1 参数分类与完整调参 GUI

参数的层级由面向谁、兼容承诺和用途决定；不是把所有 `AnalysisOptions` 字段放进 Advanced。

| 配置区 | Normal | Advanced：稳定且可迁移 | Debug：与 AC 版本绑定 |
| --- | --- | --- | --- |
| 输入和任务 | 分析选区/可视范围、启动/取消、当前 preset | 精确范围、分析精度/window preset、完整配置摘要 | 实际 PCM/窗口调度、内部阶段及版本信息 |
| Tempo | 推荐结果、歧义提示 | BPM 搜索上下限、default/local 偏好、窗口配置、fine tracking 用户策略 | tempo agreement、候选数量、onset gate、anchor reliability、transition/slope/phase penalty 等内部字段 |
| Phase / gap | 对齐与覆盖提示 | 可信 gap 策略和模型/导出误差预算 | phase back-propagation、内部连续斜率、修正机制与阈值 |
| Rhythm | 可用结论和未实现说明 | 显式复杂分度开关；稳定支持的分析策略 | 高事件率搜索范围、periodicity/semantic gates、support window 数、分度上限和其他实验参数 |
| Transient | 峰与曲线开关 | 已有相对阈值、最小间隔、频带及过滤选项 | 原始 flux、局部阈值与逐峰接受/拒绝字段 |
| Proposal/export | 查看影响、显式 Apply | 最大误差/最大条目、恢复 End BPM 等稳定用户选项 | 模型参数、量化/cache drift、逐节点误差和失败原因 |

Advanced 中的内部策略先形成稳定的用户语义和映射，无法承诺兼容的参数继续放 Debug。显示开关、zoom、tab 和 viewport 属于 UI 状态，不混入算法 options。

为 AC 当前声明的可配置字段建立参数描述表：ID、类型、单位、默认值、上下限、section、层级、说明、相互约束、支持的算法版本。Debug 按 section 提供字段编辑、默认/覆盖值对照、单项/section reset 和配置差异；不是只提供可编辑 Raw JSON。

验证至少包括 finite、min/max 顺序、window duration/hop、合法数量及导出预算。验证失败时保留用户输入和原因，不启动任务。AC 尚未参数化的内部常量只能显示其诊断值；不为了调参 GUI 将所有硬编码 gate 改成公共 API。

CCE 产品 Default 与 AC 原始默认值分别说明。例如复杂分度在 CCE 默认关闭，而 AC 原始 options 默认开启；Default 必须是 CCE 完整有效配置，不从 AC 默认推测当前产品行为。

### 3.2 Config / Profile 的完整闭环

Profile 操作包括：只读 Default、当前配置、Save As、复制 preset、按 section reset、全量 reset、JSON import/export、修改标记与有效配置差异。

配置结构及兼容：

- 稳定区保存 `schemaVersion`、稳定字段与迁移信息；AC 内部参数置于独立 Debug section，记录 algorithm version/commit 和 capability 信息。
- Advanced preset 需要长期兼容和显式 migration；不支持的字段显示迁移报告。Debug 字段在 AC 更新后允许改名/废弃，但必须列出未生效项，不能静默当作当前值。
- 导入先解析和验证、展示配置差异，再替换当前配置；不会自动分析或改谱。未知/旧字段可保留在原导入记录中，未支持项不参与有效 options。
- 有效配置合成为 CCE/AC 默认值 → 所选全局 preset → project override → 当前 session 的未保存修改；每次运行冻结最终有效 options，不只存 preset 名称。
- project override 存于 CCE sidecar/project 数据，保留 `.mc` 兼容；全局 preset 是用户配置。splitter/tab/zoom/Debug 展开状态留在 workspace/session，不写入分析 preset。
- canonical snapshot/hash 包含字段实际值、默认值来源、算法版本及有效 Debug overrides；相同 preset 名称不能替代相同配置证明。
- `.ccepr` 未来携带该次分析 snapshot/hash 及版本信息，作为可复现 metadata，不反向调参 estimator；本轮定义可嵌入数据结构，完整 Review 格式实现仍属于其自己的计划。

本地 Profile 保存和 JSON 导出采用已有原子写入模式。当前 dirty 配置的 Save As / reset / import 交互需要明确谁被保存或替换，Default 始终不可覆盖。

### 3.3 全曲 overview 与工作区布局

增加类似主编辑器 density 导航条的全曲音频 overview，显示 RMS/peak 或清楚标注的相对能量包络、当前 viewport 和播放位置。它的语义是音频能量，不能复用 Note density 值，也不把 RMS 标成未经实现/校准的 LUFS。

点击定位、拖动 viewport、显示选区与循环范围；时间经过同一 transform。zoom 切换、musical/absolute view、前奏/尾段及翻转均保持对应位置。

overview 采用后台全曲聚合、少量显示桶和 source-keyed cache；局部 Spectrum page 与完整 overview 明确区分。缓存失效随音频身份，不在拖动时重新解码全曲。随机访问 PCM cache 列为音频基础设施后续项，与第一版 overview 的低内存聚合分开。

## 4. 详细 Debug GUI

四组 tab 来自原验收计划，复用现有表格，增加曲线、关联与摘要。

完整 Debug 包含第 3.1 节参数面板和以下结果面板；必须能将“使用的参数 → 输入证据 → 中间选择 → 最终结果 → 失败”串起来。

### 4.1 Overview / Compare（输入、模式、最终选择与版本）

显示 Legacy BPM 和 legacy offset；V2 推荐 BPM、family、pulse 位置；所选 hypothesis 和 alternatives；分析状态、覆盖、耗时和主要失败原因。

输入摘要记录音频身份、请求区间、实际分析区间、解码后采样率/声道/样本数、重采样或 mono 转换、完整 options、CCE/AC 版本、任务 ID。

比较方式：

- 同输入的 default 与 local 结果并排或叠加；复杂分度开关作为独立维度记录。
- 切换同一结果内的 hypothesis 属于浏览已有输出；改变分析 options 才创建新任务。
- 导入已有 baseline/candidate diagnostics，显示输入、配置和版本是否可比。
- 比较区间包括共同覆盖与各自丢失区间。输入不一致时仍可浏览，但不能给出可比精度结论。

同输入比较应复用一份冻结的解码输入，或校验解码后 PCM 身份；不能只凭同名文件认为输入相同。初版不内置全曲 corpus runner，不在打开 tab 时启动测试。

### 4.2 Tempo / Phase（观测、曲线、残差与覆盖）

联动展示窗口观测、所选 tempo track、segments、phase model、anchors、BPM approximation，以及不确定/缺失区间。

重点支持：

- 原始观测、修正后观测、所选模型使用不同图例；能看到拒绝点、gap 和首尾覆盖。
- 按 window / candidate / anchor / segment 选中并交叉定位；可将其时间范围送到现有选区和 loop。
- signed residual 曲线及 P50/P90/max；明确标注残差类型、样本集合及覆盖。
- 区分观测对模型、导出 BPM list 对模型、独立参考对结果三类误差。
- half/double family 变化、选中分支变化、stable regularization 和变速边界显示其来源与原因。
- 没有 pulse 的 tempo-only candidate 不提供 phase 定位；无模型的 hypothesis 只显示其已有 track/字段。

沿用现有恒定候选网格；拟合模型预览使用 AC 已发布的模型参数。UI 不拟合第二套 estimator，也不把缺少的 alternative phase curve 编造成完整结果。

### 4.3 Rhythm / Evidence（周期证据、语义候选与拒绝原因）

raw periodicity 与 semantic rhythm 分开显示。字段按 AC 实际能力提供：rate/period、rational ratio、support windows、source/band、置信和相位支持、时间范围；closure span、root/refinement、macro cycle 等没有输出的字段标 unavailable。

展示复杂分度开关前后结果、跨窗口持续性、通过/拒绝的 gate 与原因。高倍率事件率不能自动解释为结构 BPM；raw 有理关系不能自动声称已识别复节奏。meter/downbeat/swing 尚未实现的能力明确标注。

优先补齐已经存在于 AC、尚未经过 bridge 的窗口字段：`SignalMetrics`（RMS、peak、silence/clipping、dynamic range、transient density、signal/transient score）、完整 `EvidenceReason` 和 raw `rhythmCandidates`。按 enum 稳定字符串展示拒绝原因；未知原因保留原值。

窗口详情同时显示 signal/transient evidence、tempo evidence、cross-scale support、reliability 和最终 anchor disposition；计算链中的公式或分项只有 AC 已输出或其版本化确定公式可直接解释时才展示。对于尚未记录的候选过滤 gate，不从最终候选池倒推原因。

将证据区间送到频谱/Transient 进行观察，保持音频时间一致；该页不生成 BPM changes 或 notes。

### 4.4 Reference / Tests（现有记录、参考可信度与失败定位）

初版是已有本地记录的查看器。导入 diagnostics / comparison / benchmark summary，展示 case、音频与参考身份、baseline/candidate、配置、指标、失败区间和原因。

状态分别为 PASS、FAIL、SKIP、MISSING、UNANNOTATED；分析完成状态另列，不能用 Succeeded 替代准确度 PASS。

参考区分 synthetic truth、作者参考、partial timing reference、机器假设、audio-only。参考只进入 evaluator；音频/参考身份不匹配时不做精度判定。没有标注的歌曲可报告一致性和覆盖，不报告准确率 PASS。

失败行定位其绝对音频区间，可选择 loop。保存比较结果为本地 JSON。沿用已有约束：截图只作为必要运行验证材料，UI 不增加截图产品功能，也不上传音频或私有验证包。

## 5. 数据与状态设计（支撑三层的最小边界）

将分析任务及结果从大型 QWidget 中逐步抽出，建立轻量的不可变结果记录；不引入全项目框架重写。

每条结果至少包含：

- run ID、request/config revision、source identity、请求与实际区间。
- 完整 options 快照、解码/转换信息、算法和宿主版本。
- Legacy 与 V2 各自的状态和错误；共享准备阶段错误单独保留。
- bridge 原始结果、选中的 hypothesis、派生显示数据的来源。
- 可选 baseline/reference identity、比较可用性、所有失败原因。

状态流程：Idle → Running → Ready / Failed；取消后进入 Discarded，worker 尚未退出时明确显示等待。当前 AC 没有中途取消 hook，不能把“丢弃结果”显示成“计算已停止”。

配置改变时生成新配置版本；旧结果保留其完整快照并标为旧配置。正在运行的旧任务可以等待并存入历史，但不能覆盖当前配置的结果视图。切换音频则立即清除其 preview 和当前结果，并拒绝旧回调。

文档 timing / note revision 用于应用 proposal 的有效性判断；纯音频分析不因普通 Note 编辑而失去其音频证据。不能把 source、request config 和 document revision 混成一项。

时间与数值语义：

- 音频绝对 seconds/ms、chart beat 三元组、legacy offset 三者明确分开。
- crop 平移只做一次；时长、period 不平移；图、表、定位和导出统一使用结果时间基准。
- null / unavailable 保留；真正的 0 与缺失值区分。pulse index 保持十进制字符串。
- objective cost、raw score、置信字段保留原语义；未经校准的 score 不写成概率。
- JSON 增加 schema/version 和 options/provenance；兼容已有导出，导入时处理不支持的版本和字段缺失。

关键代码边界：`AnalysisEditor` 负责窗口编排；`AnalysisCanvas` 负责音频坐标与 overlay；独立 panel/model 负责表格、曲线和选中关联；`AutoTimingDiagnostics` 负责序列化；`AutoTiming2Bridge` 只投影 AC 输出。

### 5.1 统一 Analysis data / tracks

为 Spectrum / Transient / AutoTiming 提供轻量纯数据适配：Event、Curve、Region、Candidate、Track 及其 source/provenance、confidence、disposition、time range、jump target。保留现有标准库分析输出，用适配层进入共同轨道，不重写 DSP。

音频分析轨道保留绝对音频/sample 坐标；musical 显示投影附 transform ID。窗口/候选之间通过明确的引用关系联动；分析对象可有 session 稳定 handle，这不要求所有 Note 新增永久 ID。

轨道支持 visibility、图例、z-order、viewport clipping、只读/可交互能力；绘制和命中由 UI 管理。Tempo、phase residual、confidence、uncertain regions、Transient 均由同一轨道接口进入频谱时间轴，避免每项结果继续增加 canvas 专用状态。

### 5.2 Session / task / view 职责拆分

渐进拆出以下职责，名称是建议，不要求一次完成大型重构：

- AnalysisSession / TaskCoordinator：source、request、result history、任务与结果归属。
- AnalysisConfig / ProfileStore：描述表、验证、有效配置、迁移和 snapshot。
- DiagnosticsModel：已有 DTO/JSON、bridge 解释字段、选中关联与比较指标。
- ViewCoordinator：musical/audio transform、viewport、overview、轨道和定位。
- TimingProposalService：手动测量、插值和 AC proposal 的共同验证/preview/apply 边界。

任务接口对齐 CCE 的 TaskContext 方向：task ID、source identity、需要时的 document revision、config revision、cancel、supersede、status/progress。当前没有完整通用 TaskContext 时先形成可复用的小接口，不声称该框架已经存在。

当前 AE 每窗仅一个 timing worker，不把“取消不终止计算”误报成已证实的无限并发任务 bug。待 AC 独立迭代时增加 cancellation checkpoints，并将实际停止状态经 bridge 返回；在此之前保留等待提示和串行限制，不报告虚假的精细进度百分比。

### 5.3 共享 Timing proposal / preview / validation

Measure、Interpolation、AutoTiming 都生成 proposal；来源计算各自保留，共享输入 revision、替换区间、preview tracks、误差/覆盖校验、影响摘要、显式 Apply 和 timing-only undo。

恒 BPM candidate grid 继续作为局部参考；完整 map preview 使用连续/分段模型生成的 grid，显示不确定和无覆盖区间。不能用一个 `bpm + pulse` 等间距网格代替变速 map。

手动测量默认 1 beat、支持小数/分数，Start 保留三元组并吸附，End 保留实测 ms。插值最大间距默认 1 beat、内部误差保持既有严格小于 10ms 的实现、自适应细分和节点时间补正；共用层保留 source triplet、actual cache drift 和 duration residual。

无可靠 AC map、歧义/低置信或超过预算的 proposal 不可应用。保留 offset 和 Note 拍坐标；有 Note 时清楚说明音频落点变化。假变速、分度、tempo-only 证据不能越过这条边界。

Note/Rain 的增删移动、尾拖、selection、Undo/Redo 和 Delete 使用共享编辑服务及 AE 上下文命令。保留主编辑器空间命令隔离；不把其现有 x 移动行为硬套到 time-only lane。

## 6. AE 修复清单

用户给出的历史清单包含明确 bug、架构债务、功能缺口和算法风险；分开登记，避免把所有事项都称为已经复现的 bug。本节先列当前代码核对项，再将历史清单完整归入实施范围。

| ID / 优先级 | 问题与证据 | 修复方向 | 验收 |
| --- | --- | --- | --- |
| AE-01 / 高 | 结果与配置状态：`AnalysisEditor.cpp` 的 start/duration/complex 控件未连接结果失效；`runTiming` 仅绑定 source/task generation。配置改变后界面可保留旧结果，运行中的旧配置结果也可返回当前视图。静态可见，运行复现待做 | options/request 快照与配置版本；旧结果标签；当前视图拒绝旧配置回调 | 完成后、运行中分别修改参数，结果都明确归属原配置，当前预览不冒充新结果 |
| AE-02 / 高 | 选区语义：`createTimingPanel` 使用 `setRange(4, 300)`，Use range / Use visible 通过 spin box 设置时长，短/长区间会被截到上下限；未见剩余音频检查。静态可见 | 明确请求起止；保留选区精度；不足最小分析长度则解释不可分析；长范围显式分段或说明限制；EOF 前复核实际区间 | 短于 4s、长于 300s、起点靠近 EOF 时不默默分析别的范围；请求/实际区间一致可核查 |
| AE-03 / 中 | 集成缺口：bridge 已有 `preferLocalTempoEvidence`，Measure BPM dialog 已暴露；AE 仅设置复杂分度选项 | AE 暴露 default/local；默认沿用 false；和复杂分度开关分开；完整写入结果快照 | options 实际传到 bridge；两模式结果独立；变化触发 AE-01 规则 |
| AE-04 / 高（待复现） | 同路径替换音频：`refreshAudioSource` 使用 path/size/mtime，但 timing 回调只检查 generation；若替换发生后没有触发 refresh，可能接受旧源结果 | 回调接收前重新核对源身份；内容 hash 用于持久比较与参考匹配，不在 UI 线程同步扫描大文件 | 任务运行中同路径换文件，旧结果不进入当前音频；保存比较可识别 PCM 不匹配 |
| AE-05 / 中（边界审计） | 正在测量、Rain tail、range 或插值 pick 时切层/关面板/隐藏窗口的手势与 overlay 状态需要核对；已有 `cancelGesture` / `stopPicking` 可复用 | 所有新入口复用既有取消路径；草稿状态和结果历史明确区分 | 无幽灵拖拽、误创建、残留预览或错误 undo；不重复实现现有正常行为 |
| AE-06 / 中（已知限制） | 长音频后段频谱仍顺序解码；100s 可视范围、120s page 和 page-relative transient 阈值均见实现说明 | 首轮先显示分页范围、缓存/解码状态及阈值范围；随机访问 PCM cache 独立后续项 | 无静默截断；跨页绝对时间正确；不把翻页带来的阈值变化解释为算法异常 |
| AE-07 / 高（解释缺口） | AC window 已有 SignalMetrics、EvidenceReason 和 rhythmCandidates；bridge window 目前未完整投影这些字段 | 扩充只读 DTO、crop 平移及 JSON 输出，供窗口 evidence 详情使用 | 每个字段来源可核对；枚举、null 和 raw/semantic 语义不丢失 |

`BpmMeasureDialog` 已有修改选项后结果失效、按音频时长设置时长范围的模式，可参考其行为；不盲目复制全部 UI 逻辑。

### 6.1 历史问题的当前状态与归属

| 历史项 | 当前状态 / 判断 | 计划落点 |
| --- | --- | --- |
| 时间坐标未统一、变速模型不足、ms/px zoom | 已有 chart BPM cache 与连续 AC 模型，但独立视图仍以 ms 为主；属结构性缺口 | §2.1：统一 transform、musical view、原始 triplet 及低证据语义 |
| bridge 无法解释全部 reliability 来源 | SignalMetrics/reasons/raw rhythm 的缺口由源码确认 | AE-07、§4.3：只读 evidence 和来源链 |
| 调试强、调参弱 | AE UI 仅复杂分度开关，bridge 可配置子集较小 | §3.1：完整字段描述表，Advanced 与 Debug 分层，渐进扩充 bridge options |
| Advanced / Debug 兼容边界未落地 | 当前没有完整稳定配置模型 | §3.2：稳定 schema migration，版本绑定的 Debug overrides |
| Profile、全局 preset/project override、snapshot/hash | 当前没有完整闭环 | §3.2：Default、当前、Save As/复制/reset、导入导出、有效配置快照 |
| 表格/Raw JSON 为主，可视化不足 | 已有 candidate grid 和 Transient；tempo/confidence/uncertainty/residual overlay 仍需统一 | §4、§5.1：曲线与四组 Debug tab |
| Spectrum/Transient/AutoTiming 未形成统一轨道 | 多个相邻子系统，已有可复用纯数据输出 | §5.1：最小 Event/Curve/Region/Candidate/Track 适配 |
| AnalysisEditor.cpp 职责膨胀 | 窗口、任务、表格、导出、持久化集中在一处 | §5.2：session/config/diagnostics/view/proposal 分离 |
| 异步机制未统一 | 已有 generation + atomic cancel；通用 TaskContext 仍属基础设施方向 | §5.2、AE-01/04：先收敛小接口和正确失效条件 |
| AutoTiming 取消不终止计算 | 明确现有限制；每窗串行运行已防止该窗直接堆 timing worker | §5.2：诚实状态；AC checkpoint 单独后续项 |
| 缺全曲音量/能量导航 | 当前只见局部频谱/峰与 RMS gutter，没有所述完整 overview | §3.3：全曲低内存 envelope 导航 |
| Spectrum 右侧 Note lane、spare 占位、宽度策略 | 右侧 lane 和持久化已存在，spare QWidget 也仍在 | §3 布局：明确 work area sizing，保留窄 lane |
| Measure / Interpolate / AC 关系未统一 | 三者工具已存在，公共 proposal 层仍缺 | §5.3：共同 validation/preview/apply，保留各自计算 |
| 插值精度及表达 | 已有 beat-linear 积分、解析内部误差、实际 cache 检查、默认 1 beat / <10ms | §5.3、§8：保留性质，验证共用层造成的实际变化；全曲测试保持停止 |
| 候选预览偏恒 BPM | `setTimingPreview` 为恒定 bpm/pulse/range，符合历史描述 | §5.3：保留局部 grid，新增完整 continuous/segmented map preview |
| 复杂节奏 vs 真变速风险 | 算法风险仍存在；GUI 不能独立宣称解决 | §4：tempo/rhythm 成对解释、失败与覆盖；AC 算法项独立保留 |
| 停顿/低证据区间语义 | 当前已有 uncertain regions，需要统一视觉/模型延续语义 | §2.1、§4.2：维持可信 tempo、标 no evidence，不置零或重启 phase |
| confidence 不易理解 | 多级数值存在，面向谱师解释仍不足 | §3：Normal 自然语言、Advanced 解释来源、Debug 原值 |
| Note/Rain 与 analysis 操作分裂 | 当前已有共享 controller/selection/undo，AE 增删仍直接调用 controller | §5.3：AE 上下文 Command/Action 与既有编辑服务，保留空间隔离 |
| 全 CCE CommandRouter 仍在迁移 | 历史时点过时：统一快捷键已有完成记录；AE 工具模式和 Delete 仍需局部审计 | 不重启全局迁移；只补 AE 上下文行为及相关用例 |
| UI、project、analysis metadata 状态混放 | 现有 QSettings 主要存布局/zoom；未来配置需分开 | §3.2、§5：workspace、global/project config、per-run snapshot 分离 |
| corpus 尚未体系化 | manifest 和全曲 runner 已有；每曲独立语义参考及缺失资源仍不足 | §4.4、§7：读取现有记录，保留 UNANNOTATED/MISSING，不重复建池 |
| 截图被当成交付功能 | 原计划与移交已有明确边界 | §4.4：结构化记录为主体，截图仅必要验证材料 |
| AE 只在开发分支 | 历史时点过时：当前已在 v2-main | 按当前主分支维护；未实现能力和接口兼容范围仍逐项标清 |

## 7. AC 当前遗留项（供 Debug 展示，保持独立）

以下来自本地收尾记录；本轮没有重新验证或修复。

- 《侮辱嘲笑》default 半幅度一致性回退：164s / window 40，287.840770 BPM 替代峰的加权 score 0.090609405；signalScore 从 1 降为 0.925648953 后触及 0.09 过滤边界，观察选择由 143.920385 变为 158.290532 BPM。候选保留修复尚未实施。
- selected-output 合成矩阵仍为 5/10 严格通过；decelerando 两模式、slow ramp local、smooth reversal 两模式五项仍失败。
- 六项 local 首尾覆盖缩短，最大 0.214204s；无独立标注，不能据此判定歌曲精度。
- 实曲全为 UNANNOTATED，七份精确上游参考资源仍缺失；现有 CTest 通过不能替代准确度验收。

Debug 应能展示上述失败、选择过程和覆盖变化。现有 DTO 没有某个过滤/选择细节时，可单独规划 AC diagnostics 字段扩展；在实际输出前标缺失。候选 AC 工作树不自动替换生产子模块。

独立的算法修复与 gitlink 更新留待后续明确进入该阶段。已停止的实曲、幅度变换、全矩阵和性能测试保持停止。

## 8. 实施拆分与验证范围

| 顺序 | 交付内容 | 范围与完成条件 |
| --- | --- | --- |
| 0（本轮已完成） | 历史需求和当前状态核对 | 三层和已知问题已由用户补齐，源码缺口和过时项已登记 |
| 1 | AE 状态与范围修复 | AE-01/02/03，复现后处理 AE-04；result/request/config identity 正确；避免继续在错误状态上扩展 GUI |
| 2 | 时间和数据地基 | musical/audio transform、最小 Analysis tracks、session/task/view 分工；保留已有纯数据与 controller |
| 3 | Config / Profile 与参数 bridge | 稳定 Advanced schema、Debug capability/参数表、Default/Save As/复制/reset/导入导出、project override、snapshot/hash |
| 4 | 三层与高级可视化 | musical view、全曲 overview、紧凑宽度策略、自然语言置信说明、map grid 和共同 proposal/Command 边界 |
| 5 | 完整 Debug 工作台 | 补 SignalMetrics/reasons/raw candidates，四组 Debug tab、调参、输入/配置/选择链及本地记录比较 |
| 6 | 针对性验收与文档 | 按实际改动做必要的 UI/数据/bridge 验证；更新 AE 说明和工程 TODO；AC 算法/取消 hook/gitlink 独立后续 |

实施集中在 `feat/ae-analysis-workbench`，各模块保持可分开审查。复用现有数据、任务与编辑接口，没有创建空壳全项目框架。

后续实现阶段的必要验证以现有 Qt GUI / bridge 用例为基础：

1. 配置改变与异步返回：参数编辑、取消、隐藏、切音频、同路径替换，检查正确结果归属；Profile migration、Default 只读、section/full reset、项目覆盖、snapshot/hash 的完整值。
2. 范围与时间：短/长/EOF、非零 crop、offset、BPM 阶跃/连续 map、非规范 beat 三元组、musical/absolute view、翻转视图，检查实际区间、可见中心和定位。
3. 诊断语义：SignalMetrics/reasons/raw candidates、null、tempo-only、失败、低置信、raw/semantic、旧 schema、输入/参考身份不匹配；无证据保持 tempo 且标 uncertainty。
4. 联动与无修改：行/曲线/失败区间定位，选区和 loop，共同覆盖与丢失区间；浏览、比较、导出不改 chart/snap/undo。
5. 应用边界：沿用手动工具的精确 triplet、proposal revision、有 Note 确认及一次 undo；新 AutoTiming map 应用单独验收。
6. 布局与编辑：层级切换、窄窗、缩放比例、折叠/恢复、全曲 overview 拖动和关闭隐藏，检查画布可用面积及草稿取消；AE 上下文 Delete/移动/Undo 重绑不泄漏主窗口空间命令。

UI 优先使用现有本地 diagnostics 和人工构造的数据验证显示，不重新跑整批音频。修改算法、更新 gitlink 或重新启动 AutoTiming 测试属于后续独立阶段；不能由打开 Debug 或实施普通 UI 整理隐式触发。

## 9. 可直接采用的实现决策与后续边界

- 三层入口放在同一 AE；Normal 默认，Advanced/Debug 逐层展开。Debug 首版沿用可收起的工具区，不新增独立应用或另一份谱面 session。
- musical-time 为谱面编辑主视图，absolute-time 为测量/诊断视图；分别保存 zoom，保持共享音频游标。
- Advanced 配置稳定迁移；Debug 按 AC capability/version 显式解释。Default 只读，项目 override 和 workspace 状态分开。
- 分析纯数据、配置、任务和视图逐步拆出；采用已有接口形成的最小抽象，不等待全 CCE 基础设施重写。
- 手动测量、插值、AC map 共用 proposal 边界；候选、参考和测试浏览都不直接写谱。
- AC 真正取消、候选保留与剩余 strict 精度问题、随机访问 PCM cache、完整 `.ccepr` 封装是独立后续项；本计划给它们明确接口和归属，不将其现有失败抹去。

## 10. 实施记录（2026-10-08）

| 范围 | 已落地行为 / 验收点 |
| --- | --- |
| AE-01 / AE-03 | `AnalysisSession` 冻结 request/config snapshot/hash；配置改变使旧结果失效并取消旧请求。local evidence 与复杂分度独立传递，串行调度；注入任务回调验证旧配置拒绝、失败和销毁后的回调保护，不调用 Core |
| AE-02 | 起止保留六位秒精度，取消 4–300s 静默截断；最低窗口、EOF、调度与 PCM 预算显式拒绝，不分析截断后的区间 |
| AE-04 | worker 中对编码文件做前后 SHA-256 核对，记录实际 PCM hash/格式/采样率/帧数及 crop origin；UI 接收前复核 source identity；比较与 case 导航严格核对各自输入身份 |
| AE-05 / 编辑 | 切层级/面板/隐藏复用手势取消；Note/Rain time-only 拖动保留 x 和精确时长，AE Delete 与共享 undo 使用本窗口上下文 |
| AE-06 / overview | Spectrum 保持已有明确分页与状态；新增后台全曲 RMS/peak 低内存聚合、导航、选区/loop/viewport 显示及源缓存。随机访问解码仍为独立项 |
| AE-07 / Debug | bridge 增补 SignalMetrics、完整 EvidenceReason、raw rhythmCandidates；四组工作台、hypothesis、残差、原始记录与同输入 baseline 曲线；未提供能力保持 unavailable |
| 时间 / proposal | 共享 musical/audio transform，musical view 默认、独立 zoom；模型曲线网格与 uncertainty overlay。三种 timing 工具共享验证、影响预览、确认后 revision/source 检查及一次撤销 |
| Profile / 参数 | schema 1 稳定区、Core 版本绑定 Debug 描述表；Default 只读，Save As/copy、单项/section/full reset、导入差异确认、JSON 导出、全局 preset、稀疏 project sidecar、完整有效快照。损坏全局文件保护 |
| AE-08 / 新发现 | 非零 offset 的 tempo-map 投影原来先减 offset 再在残差上加回 offset，可能接受错误拍点；修正为只经现有 chart 变换处理一次 offset。人工 phase map +125ms fixture 验证边界、显式确认、Note/offset 保留及一次 undo/redo |
| 原生运行环境 | 测试目标独立部署 Qt runtime/Test/offscreen 插件，CTest 使用部署后的平台插件目录；测试字体取系统字体。修复截图中的平台插件初始化失败，无需重装 Qt |

完成审计补齐了两处闭环：`AnalysisTracks` 将 Spectrum 包络、Transient 曲线/事件、Core 曲线/区间/候选适配为共同纯数据 Track；`AnalysisTrackScene` 共用显示、z-order、裁剪和只读命中定位，并保留 source/provenance、可缺失的 raw confidence、disposition、音频/帧坐标及 transform ID。当前/baseline tempo 同轴比较，tempo-only candidate 无跳转目标，事件不伪造 confidence；隐藏的可选轨道不构造显示投影。GUI 选中对象关联表格、音频和区间，不产生 Note/undo 修改。

Project sidecar 现保存 base preset 的完整 snapshot/name/hash 与稀疏 override；按 base 验证参数约束，重开以及缺少/不同全局 preset 时保持同一有效配置，通过只读 Project base 显示。损坏 snapshot/hash 或不可读 sidecar 会阻止分析并给出恢复/清除提示。全局 preset 名称是工作区偏好，算法值不混入 QSettings。

`AnalysisEditor` 负责连接视图，配置/Profile、任务、工作台、overview、共同轨道与 proposal 分别归独立模块。通用 CCE TaskContext 仍属于独立基础设施方向，不影响本轮共同轨道接口的完成。

最终验证：Qt 6.10.2 / Windows / Visual Studio 原生 Debug、Release 的 `CatchChartEditor` 和 `AnalysisEditorTests` 均构建成功；28 个定向宿主用例在两种构建中各通过一次（各 30 条 QtTest PASS，含 init/cleanup；Debug 32.958s，Release 5.719s）。配置/Profile 持久化、base 继承/移植、文件保护、异步请求失效、共同轨道来源/变换/只读命中、时间/模型域、原始字段、同输入比较、synthetic WAV 全曲 envelope、Note/Rain 编辑、测量/插值/map 应用及撤销均在范围内。P50/P90 使用有效 anchor/model 绝对残差的线性插值分位数，并以两点夹具验证。

检查了 Normal、Advanced、Debug、Tempo/Phase 和 Configuration 五张人工数据夹具截图：文字可读、四组 tab 在窄侧栏可见、配置控件无需横向滚动、紧凑右侧 Note lane 与频谱时间一致。截图及 txt/JUnit 日志仅保存在 `artifacts/ae-workbench-20261007/`；该目录名沿用本次迭代起始日，最终验证日期为 10 月 8 日。首次 Release 编译器曾崩溃，重试及最终构建均成功，没有降低优化或更改算法来绕过。

未运行 `realAudioAndTimingPipeline`、`externalAudioInspection`、完整 CTest、Core estimator/accuracy/corpus/smoke。这里只验证宿主，不据此声称歌曲精度已通过。独立 Core 七个暂存文件、生产 `e47016d` gitlink 和已停止测试保持原状态，第 7 节算法失败仍未解决。全部实现仍为本地可审查变更，未自动提交或推送。
