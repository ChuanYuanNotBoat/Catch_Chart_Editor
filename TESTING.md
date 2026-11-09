# Testing Guide

> 适用版本：Beta v1.12.0
> 最后核对：2026-10-24

## v1.12.0 发布核对（2026-10-24／25）

2026-10-25 补充经典侧栏整合和音符属性迁移，新增 `ClassicSidebarTests` 后共 39 个目标。发布验证范围包括 Release 的 `ClassicSidebarTests`、`ShortcutTests`、`ReviewWorkspaceTests` 和 `DockingLayoutTests`，直接使用 `QT_QPA_PLATFORM=offscreen` 且关闭音效；执行记录另存于 `build/v1.12.0-ui-*`。以下 38 个目标的结果属于此次 UI 调整前的基线，不能当作最终 39 个目标已全部重跑。

当前功能边界和最终发布条件见 [RELEASE_READINESS.md](docs/RELEASE_READINESS.md)。本节记录当前工作区的验证，以下各日期段保留历史范围。

- Debug、Release 主程序及全部测试目标构建通过，运行程序报告 `Beta v1.12.0`。打包脚本语法、版本一致性及运行资源检查通过；未编译安装器或执行安装／升级。
- Release 首轮运行到第 33 个目标时，按用户“不要弹窗、不要抢焦点、静音”的要求中断；此前 30 个目标通过、2 个目标失败。后续只运行无界面、无音频输出的检查：上游 8／8（49.64 秒），修正后的审阅与资源复制 2／2（6.56 秒）。分段结果按目标合计 38／38，不能表述为一次完整连续运行通过。
- Debug 静默集合 25 个目标耗时 824.33 秒，原有 1 个配置断言失败；修正后的审阅与资源复制 2／2（29.41 秒）通过，最终有 26／38 个目标的通过结果。12 个未运行目标为 `HyperfruitTests`、`ChartMergeTests`、`ReviewInspectorTests`、`ui_grid_drag_tests`、`audio_playback_timing_tests`、`ui_spectrum_overlay_tests`、`analysis_editor_tests`、`ui_shortcut_tests`、`core_minimal_tests`、`ui_docking_layout_tests`、`ui_meta_edit_panel_tests`、`ui_bpm_measurement_tests`；本轮不启动这些目标来补齐数字。
- 新增配置回归使用带点的谱面文件名，验证源文件旁 `.analysis-config.json` 导入工作副本、修改后随保存返回、纯净 MCZ 打包及递归发现谱面后恢复配置。
- 首轮 `ReviewWorkspaceTests` 的三项像素断言沿用旧的零时刻视野，导致测试目标在画布外；现在显式定位到覆盖测试拍点的视野，并先检查取样点在图像内。配置测试按实际 MCZ 子目录寻找谱面，保留文件名和内容比较。两个目标在两种配置中均已复核通过。
- Release 审阅工作区定向记录为 46 项通过、1 项真实谱面副本检查跳过；资源复制为 6 项通过、1 项已安装长音频检查跳过。没有提供外部真实谱面／音频输入，不将跳过项解释为验收通过。
- 三种语言现有活动条目无未完成翻译，替换参数一致；文档相对文件链接及 `git diff --check` 通过。此前中文 20 条、日文 22 条审阅标签遗漏已补齐。

原始构建、CTest、诊断及定向复核记录保存在忽略目录 `build/v1.12.0-*`；汇总为 `build/v1.12.0-validation-summary.json`。原始失败与中断记录保留，不用最终重跑结果覆盖历史日志。静默用例不替代原生窗口焦点、实际听音、Malody 客户端和安装升级检查。

## 谱面合并

`ChartMergeTests` 覆盖只读来源导入、倍速与变调/保调、BPM/delay/offset 和零间隔的音符时间一致性、Rain 尾点、MCZ 往返、分段图层预览工程、取消时原子输出，以及预览/关闭重开后保留窗口参数。
运行 `ctest --test-dir build_clion_msvc -C Debug -R '^ChartMergeTests$' --output-on-failure`；Release 同理。

2026-10-23 验证：Debug 完整 CTest **38/38**（574.53 s），Release **38/38**（120.00 s）。
最终合并集在两种配置均为 **21 项通过**，补充了无 delay 的零间隔分度与真实 MainWindow 导入，
确认预览创建独立图层、合并工作目录移除后音频和工程仍可保存；中文深色窗口截图检查通过。
日志位于忽略的 `build/merge-*`。首次并行 Debug 构建遇到 `CL.exe` 退出 `0xc0000005`，
单任务重建及后续两种配置构建通过，未改动编译器或优化设置；这不代表该编译器异常已被修复。

## 红果判定回归（2026-10-22）

`HyperfruitTests` 覆盖 Malody A–E 判的红果临界距离、默认 E、Pro、非整数毫秒、跨零 offset、四分之一帧容差、多押、Rain、反向移动，以及设置对话框的默认值、保存和取消、两个视图的红果缓存刷新。通过 `ctest --test-dir build_clion_msvc -C Debug -R "^(HyperfruitTests|core_minimal_tests|utils_settings_transfer_tests)$" --output-on-failure` 运行。

## 测试目标

- 保护 beat/ms 换算、谱面模型、控制器信号和文件 I/O 行为。
- 保护原生 Note Chain 的采样、状态、V3 sidecar 与宿主协作。
- 保护 ADS 可组合面板、滚动内容、浮动交接、布局恢复和 Windows 原生主题。
- 同时验证 Debug 与 Release，避免只在单一配置中通过。

## 自动化目标

### 只读文档快照

`ChartSnapshotTests` 覆盖快照只读接口、控制器销毁后的数据保留、显式副本隔离、
音符/BPM/元数据/资源域失效、Undo/Redo、文档事务、同路径重载、跨控制器来源、
无效加载与辅助评论操作、后台读取，以及未启用本地身份/工程时的标准 `.mc` 保存与重开。
5k/20k/100k 用例检查 1,000 次捕获共享底层 Note/BPM 存储并记录耗时，随后验证编辑不会修改旧快照。
此快照只覆盖活动谱面；工程合成索引、工程域事务/逆操作与完整历史内存基准仍在 TODO 中。

2026-10-21 验证：Release 完整 CTest 35/35（110.78 s），Debug 35/35（522.51 s）；
两种配置的快照集均为 15 项通过（含 setup/cleanup 和三种规模）。Windows 原生窗口下，
两种配置的快捷键集均为 19 项、网格/分组拖动集均为 12 项通过。
Release 另运行 `realChartCopyRoundTripAndGui`：4,430 Note 的真实谱面只在临时副本中加载，
验证无本地身份的旧 `.mc` 编辑保存、多轮提案回传、评论解决、应用/撤销及导出；
原谱面与资源的 SHA-256 保持一致。构建、CTest 与定向输出保存在忽略的 `build/snapshot-*` 中。

### 活动谱面的增量事务

`ChartDeltaTests` 使用独立字段序列化作为结果校验，覆盖全量可编辑元数据、BPM 原始拍点、
Note 原始顺序/重复数据/空句柄/无关字段、可空字符串、正反向序列化及绑定回放。
损坏格式、非法目标、错误前置状态、过期或不同来源版本、编辑保护期间的重入修改均会被拒绝；
故障注入检查 Undo/Redo 预检失败不移动历史、不修改辅助状态或 revision。
另有固定种子的 200 轮混合编辑回放及 5k/20k/100k 检查：16 次远距离移动仅保留 32 个 Note 镜像，
每次记录小于 1.8KB；真实 Project 解码重建所有句柄后，单个 Note 编辑仍只保留两个数据镜像，
运行期句柄映射与序列化数据分开。详情见 [DOCUMENT_TRANSACTIONS.md](docs/DOCUMENT_TRANSACTIONS.md)。

2026-10-23 当前基线验证（包含红果判定修复）：Debug 完整 CTest 37/37（562.37 s），
Release 37/37（115.79 s）。Windows 原生快捷键在两种配置下均为 19 项、拖动均为 12 项通过；
补充窗口激活等待后重跑了两种配置的原生快捷键集。
Release 真实谱面副本复核 13.924 s，通过无身份旧 `.mc` 保存、多轮提案回传、评论、
应用/撤销和导出；4,430 Note 的来源谱面及资源哈希保持不变。
完整输出见忽略的 `build/delta-oct23-*`，构建输出见 `build/delta-resumed-build-*`。

这些用例不代表完整 Workspace 长历史的内存验收：辅助 ProjectBundle 快照、原生命令 journal
以及完整版本信封/持久恢复回放仍有后续工作。

构建诊断保留：本轮 MSVC 14.51 Release 构建曾在未修改的宿主源码上出现编译器内部异常
（`CL` 退出 `0xc0000005`，另一次在 Qt `qnamespace.h` 报 C1001）。改为逐文件单任务后，
同一失败源文件使用相同编译选项直接编译成功，后续完整增量构建及 Release 回归通过。
未确定根因，也未更换工具链或禁用优化；该诊断不等同于已永久修复编译器问题。
构建输出及定向测试保存在忽略的 `build/delta-*` 中。

2026-10-21 Debug 完整回归曾为 33/36，`ChartDeltaTests`、`ReviewCoreTests` 和
`core_minimal_tests` 出现进程访问异常；第二轮为 35/36，仅增量集再次异常。
单独运行分别通过 32、18、143 项，增量集在相同 CTest 启动方式下也通过；
普通/禁用调试堆的调试器启动均正常退出，未捕获到可用于定位首次异常的栈。
这些失败记录仍保留，根因未定，不能把单次重跑通过解释成已经修复。

原生快捷键复核另曾有一次 BPM 本地撤销断言失败，单独重跑通过。夹具原先只固定等待
30 ms，现于 Windows 下先等待实际窗口激活，再发送按键；早期失败输出同样保留。

### 本地工程与审阅

`LocalIdentityTests`、`DocumentModelTests`、`ProjectStorageTests`、`ReviewCoreTests`、
`CommentModelTests`、`ReviewInspectorTests`、`ExportPipelineTests`、`ReviewWorkspaceTests`
覆盖本地署名、共享图层、难度与引用、精确 Note/BPM 差异、元数据与导出配置、
评论锚点、修订分叉与回传、包损坏回滚、资源校验、导出及应用/撤销。
所有写入使用临时工程与资源，默认不访问安装实例的谱面库。Windows GUI 夹具使用
Qt minimal 平台；可用 `ctest --test-dir build -C Debug -R 'Review|DocumentModel|ProjectStorage|CommentModel|LocalIdentity|ExportPipeline' --output-on-failure` 单独运行。

本轮还覆盖未导出 Move 草稿的保存/恢复、资源替换冲突、选择性应用 Sound 资源与整批撤销、
重开后 Sound 路径保持可移植、已接受内容改变后重新审阅、事件分支冲突及不可变修订 ID。
`ui_shortcut_tests` 验证语言切换后命令唯一、顶栏仅有页签、面板内提案入口仍可使用、评论快捷键改绑、属性面板位置，
以及工程编辑与旧 `.mc` 恢复写入的隔离。`ui_spectrum_overlay_tests` 验证播放跟随加载后续频谱。

`ReviewCoreTests` 在 5k/20k/100k Note 上检查多重集合 Diff 与整批删除/合并；
`ReviewWorkspaceTests` 在相同规模上绘制多个图层、活动 Reference、2,000 个评论线程（含回复），
并检查属性查看及列表。2026-10-19 的 Release 合成夹具连续绘制 12 帧分别耗时 17/22/58 ms，
仅作为可见区绘制的回归证据，不等同于带音频的完整谱面播放性能指标。

可设置 `CCE_REVIEW_SCREENSHOT_DIR` 并直接运行 `ReviewWorkspaceTests reviewPanelRendersAndKeepsTheViewport`、
`ShortcutTests toolbarOnlySwitchesToolPagesAndCommentShortcutRemainsRebindable` 导出本地检查截图；
Windows 外观检查使用 `QT_QPA_PLATFORM=windows`，需要能找到 Qt 的平台插件。

2026-10-20 验收记录：Release 与 Debug 完整 CTest 均为 34/34，通过耗时分别为 111.55 s、529.49 s。
最新 GUI/副本、窗口销毁后 Undo 回调及频谱播放跟随调整另外执行定向检查。
原生快捷键集在两种配置下均为 19 项通过，ReviewCore 为 18 项通过。
原生 ReviewWorkspace 在启用真实副本后为 30 项通过（Debug 107.15 s）；原生频谱叠加集为 9 项通过。
默认 CTest 不读取本机谱面，真实副本用例需显式设置下述环境变量后另行运行。

保留早期失败记录：Debug 首轮曾为 33/34，ReviewCore 大谱面用例出现一次 `0xc0000005`，
崩溃处理器仅给出 `SearchPathW` 附近的次级栈。单独重跑 5k/20k/100k、完整 Core 及后续完整 CTest 均通过，
该次 Core 异常仍没有稳定复现或确定根因。
另以 AddressSanitizer 检查 ReviewCore 的正常/冲突/包校验及 5k/20k 用例，15 项通过且未报告内存错误；
ASan 的 100k 用例触发了 QtTest 的时限，不能计为 ASan 通过证据。未据此将首次崩溃归因于业务代码、编译器或硬件。

原生窗口夹具另曾在连续销毁顶层 Canvas 后处理显示事件时崩溃，审阅与原有频谱夹具均可遇到。
独立调试器、DLL 导出跳转解析及反汇编将位置定位为 Qt 6.10.2 `processExposeEvent` 对失效窗口数据的访问；
不能把崩溃处理器给出的最近导出名 `QTextCharFormat::setFontLetterSpacingType` 当作实际函数。
对应的 [Qt 源码](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/kernel/qguiapplication.cpp#L3149) 用于核对事件处理路径，
并不单独证明缺陷归属。夹具现于对象仍存活时隐藏窗口并完成原生事件处理，复现用例及原生频谱整组通过；
恢复夹具清除自身测试目录中的旧备份，避免误读上一轮 100k Note 数据。生产代码没有加入等待或嵌套事件循环。

副本验收可设置 `CCE_REVIEW_SOURCE_CHART` 为获准只读的真实 `.mc`。夹具先复制源文件夹中的文件到临时目录，
加载器和所有编辑/保存操作只接触副本，结束时逐文件复核源目录 SHA-256。
本机 `Catch the Glory / Deluge Lv.24`（4,430 Note）已通过旧 `.mc` 无身份编辑保存、
第二难度导入与整谱 Reference、元数据/BPM/多选 Move、评论、GUI 接受与应用、整批撤销重做、
解决状态回传、二次导出及 `.mc` 导出。来源谱面与资源的 SHA-256 保持不变。
另外覆盖共享图层继续/独立复制/取消、无身份图层 GUI、隐藏与导出选项独立、资源失败时内存/审阅/撤销栈不变、
提案另存难度保留配置并隔离同名资源、显式依赖组及分支合并后重建依赖、过期后台 Diff 不发布、
评论卡片显示位置不改锚点和双击打开评论页、文本框快捷键兼容、销毁审阅窗口后回调失效保护。
整谱 Reference 使用预计算时间映射；该副本的 Release 完整夹具由约 108 s 降至约 17 s。
700 px 宽的审阅与图层窗口截图已检查，元数据列显示实际字段名，导出字段差异显示字段清单。
真实谱面副本检查属于 Qt 原生窗口自动交互与截图检查；不能替代用户对全部手感和长时间使用的评价，功能仍标为实验性。

### 评论入口与可视标注回归（2026-10-23）

`ReviewWorkspaceTests` 覆盖范围／Note／NoteGroup 右键入口、BPM／Meta 单选与多选、评论专用菜单、
其他作者权限、作者配色、连线亮度／粗细、NoteGroup 包围框，以及 TimePoint／TimePosition 对应参考线加粗。
新增实际拖动空区域／左侧时间栏、关闭范围显示后的空时间选择、Meta 标签页正文、未选中 Note 的右键绑定与
移动／撤销重做、普通谱面评论保存重开，以及真实 MainWindow 保存命令不弹出工程窗口的检查。
位置与时间点卡片的拖动均检查 Anchor 不变；BPM／Meta 评论检查工程保存重开。
`CommentModelTests` 补充 BPM 多选子集的删除／恢复、Metadata 所选字段状态，以及 Rain 旧 X 字段不产生审阅差异；
`ReviewInspectorTests` 检查 Rain 的 X 编辑入口隐藏／禁用。旧审阅包的内容校验哈希保持兼容。

Debug、Release 工作区整组均为 46 项通过、1 项真实谱面副本用例因未设置来源而跳过。
原生 Windows 定向评论交互 20 项通过，两种配置的原生快捷键各 19 项通过。
两种配置的文档、增量事务、快照、存储、审阅、评论、属性、导出、Meta、拖动和 Hyperfruit 共 11 组 CTest 均通过。
最终 Debug 为 153.40 s（审阅核心 126.57 s），Release 为 18.35 s（审阅核心 13.66 s）。
Debug 早期 ReviewCoreTests 曾达到 180 秒时限，隔离复核也曾超时。Rain 语义判断改为每个对象一次，
避免普通 Note 的逐字段额外查询；100k Note 的审阅测试单独使用 300 秒进程时限，其余测试时限保持原值。
早期超时日志保留，不把最终通过解释为已经确定所有耗时波动的根因。
非原生后端下快捷键整组也曾失败；未据此修改快捷键代码，使用原生窗口验证实际焦点行为。

构建中仍遇到过不同源文件中的 MSVC 进程异常（0xc0000005）。仅限制本次构建子进程亲和性并串行编译后，
Debug／Release 均构建通过，没有改动编译选项或系统全局配置；这不代表已确认异常根因。
测试夹具已等待 MainWindow 的初始恢复清理完成后再加载测试谱面，并按窗口的所有权约定提供 Skin。
这些夹具修正不改变生产界面的加载或析构逻辑。

截图设置 `CCE_REVIEW_SCREENSHOT_DIR`：`comment-annotations.png` 包含 Rain 作为视觉对照，
`metadata-comments.png` 展示 Meta 页正文。日志和截图位于忽略目录 `build/comment-fix-*`、
`build/comment-review-visuals/native/`。

### `CatchChartEditorTests`

CTest 名称：`core_minimal_tests`
入口：[tests/minimal_tests.cpp](tests/minimal_tests.cpp)

当前覆盖：

- `MathUtils` beat/ms、BPM cache、吸附与边界；
- Chart、Note、BPM、MetaData 的排序、增删改和信号；
- `BeatPosition` 整数边界、等价/假分数、极大分母与损坏分母的严格排序；Note/BPM 同拍稳定排序、Rain 精确时长、精确/投影选区边界、保存三元组和 Undo/Redo 保留原分度；
- 音频转换正常 WAV→OGG、损坏输入与取消结束、失败后删除未完成输出；`core_minimal_tests` 有 60 秒超时，避免解码错误导致整套测试无期限等待；
- `ChartController` 批量编辑、撤销/重做及细分变更信号；每次图表变更还携带单调 revision 和 `notes/timing/metadata/resources` 类型化变更集；统计、画布、密度、选择、预览与奖励缓存按相关 revision 失效；
- 5,000 Note 批量增删移动后的稳定身份、数量与排序；
- `ChartIO` / `ProjectIO` / `ChartFileSystem` 路径、扫描、资源和格式行为；
- 恢复工作副本的根目录边界、路径穿越和相似前缀拒绝；
- 非单调 Rain 结束拍下的可见区前缀索引与统计/Rain reward 一致性；
- SHA256 与诊断相关工具；
- Note Chain legacy 导入、锚点/控制柄、采样、密度、V3 元数据、CAS、损坏数据保护、事务式切谱及宿主选择同步。

### `DockingLayoutTests`

CTest 名称：`ui_docking_layout_tests`
入口：[tests/docking_layout_tests.cpp](tests/docking_layout_tests.cpp)

当前覆盖：

- 原生标题主题不提前创建窗口句柄；
- Windows DWM 边框颜色；
- 2400px 长内容不会强制拉高主窗口；
- ADS 布局序列化和恢复；
- 中等粒度工具块默认纵向同时显示、拒绝切换标签合并，并支持单块拆出浮动及恢复原排列；
- 右侧工具块停靠时只显示右上角紧凑拖拽手柄，不显示 splitter 灰线或额外外框；拖出后隐藏内部手柄并显示原生浮动标题栏；
- 实际非透明拖动预览到浮动容器的交接；
- 浮动窗口保持预览的全局屏幕位置；
- `ForceScrollArea` 和 viewport 在浮动首帧可见且尺寸有效；
- 工具 QWidget 可在 ADS dock 与旧式单一纵向栏之间往返转移且对象身份不变；旧式右栏保留按需纵向滚动并强制关闭横向滚动；
- 关闭浮动窗口会同步面板动作，动作可立即恢复面板，后续普通隐藏事件不会再次误关面板。
- 主编辑区不提供关闭动作；经典与多窗口设置状态分别持久化，清除任一模式状态不会删除另一模式状态。
- 关闭纵向栈中的任一紧凑工具块时，普通编辑面板吸收释放空间，其余紧凑工具块的外框及内部控件均保持自然高度；工具浮动后解除自然高度上限，重新停靠后恢复。
- 主编辑区的全部 splitter 祖先不可折叠，并保留 `420x300` 的最低可用交互面；拖放到主编辑区时只允许创建左/右侧区域，不允许把它变成纵向工具栈或标签页。
- 浮动拖拽轮廓随当前目标区域动态变化，便于在释放鼠标前确认最终停靠位置。

### `ShortcutTests`

CTest 名称：`ui_shortcut_tests`；入口：[tests/shortcut_tests.cpp](tests/shortcut_tests.cpp)。
与主程序链接同一个 `CatchChartEditorRuntime`，使用独立临时设置目录，直接测试
真实 MainWindow、ChartCanvas、原生曲线工具和 ADS 浮动面板：

- 改绑后执行 Undo/Redo/Delete/Copy，旧键失效；F8 移除后可重新分配。
- 禁用保存后重新创建注册表仍保持禁用，恢复默认后执行。
- 多段组合键执行、重叠范围同键/前缀冲突拒绝、互斥曲线/插件提交键复用。
- 方向键及扩展选择改绑、选择不改变播放位置、Alt+方向键切换模式不滚动。
- 输入框、对话框、弹出菜单隔离，BPM 对话框使用已配置的局部撤销键。
- 原生曲线删除/撤销、进程工具宿主撤销，以及实际浮动面板的命令派发。
- 语言切换重建菜单保留绑定且不重复注册；冲突后设置窗口不丢失编辑内容。
- 在其他面板释放 Alt 会清除高精度滚轮的模式切换累积量。
- Range Select 文本范围可区分被 `double` 舍入为同值的邻近拍点，反向输入按精确比较自动交换，Rain 仍要求尾部完全包含。

进程插件的真实通信由现有核心回归覆盖；此窗口套件验证宿主工具模式的路由。
当前 CTest 共 13 组（CCE 8 组、AutoTimingCore 5 组）。

## 本地命令

首次配置：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
```

Debug：

```powershell
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Release：

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

只运行某组：

```powershell
ctest --test-dir build -C Debug -R core_minimal_tests --output-on-failure
ctest --test-dir build -C Debug -R ui_docking_layout_tests --output-on-failure
ctest --test-dir build -C Debug -R ui_shortcut_tests --output-on-failure
```

重复检查偶发 UI 回归：

```powershell
ctest --test-dir build -C Debug -R ui_docking_layout_tests --repeat until-fail:20 --output-on-failure
```

## Release 播放性能基准

BPM 高级编辑的定向检查为 `AnalysisEditorTests` 中的
`bpmAdvancedWindowKeepsMainEditorInteractive`、`bpmAdvancedMeasurementAndModelessApply`、
`bpmAdvancedPreviewDoesNotBlockAndDropsStaleJobs`。覆盖真实入口、主区继续放置音符、
精确拍点、非模态确认与导出、一次撤销、后台任务过期及关闭不等待。
2026-10-18 的三个用例通过，记录在 `artifacts/bpm-advanced-final-tests.txt`；
既有手动测量及插值撤销两个用例也定向通过，未运行完整回归或 AutoTiming 准确度测试。

AE 生成草稿的定向检查可直接选择 `AnalysisEditorTests` 中的
`transientDraftUsesChartTimingAndFiltersWithoutMutation`、
`transientGenerationPreviewsCheckedNotesAndCommitsOneUndo` 和
`transientGenerationRejectsStaleSourceAndHonorsCancelBinding`。
这些用例使用人工频谱/瞬态夹具，覆盖网格与 offset、逐项勾选、双窗口只读预览、
一次撤销、源文件替换和取消键改绑，不启动 AutoTiming 准确度或实曲测试。
2026-10-18 的 Debug 六项定向检查及一次布局复核通过，记录在
`artifacts/ae-generation-tests.txt` 和 `artifacts/ae-generation-layout-check.txt`。

频谱优化另有 `analysis_spectrum_tests`、`analysis_transient_tests` 和
`ui_spectrum_overlay_tests`；AE 与主编辑区共存由 `analysis_editor_tests` 覆盖。
主编辑区频谱的播放对比可在以下命令中加入 `--benchmark-spectrum-overlay`，
该选项不修改用户偏好。需要静音时追加 `--benchmark-mute`，音乐和打击音效均静音，
播放计时与性能测量继续运行；追加 `--benchmark-window-size 2560x1440` 可验证大窗口。
缓存与更新边界见 [频谱实现](docs/SPECTRUM_PIPELINE.md)。

主编辑区或 MainWindow 高频路径有改动时，使用实际谱面运行内置基准。开发机的主要
压力用例是 `Yugami - Nyanpasu- Lv.16`（4,461 notes、1 BPM）：

```powershell
$chartPath = 'C:\path\to\Yugami - Nyanpasu- Lv.16.mc'
build\Release\CatchChartEditor.exe `
  --benchmark-chart $chartPath `
  --benchmark-fps 0 `
  --benchmark-warmup-ms 3000 `
  --benchmark-duration-ms 10000 `
  --benchmark-output artifacts\local_playback_benchmark.json
```

验收时至少检查：`verdict.passed`、canvas/preview FPS、paint p95、
`ui_stall_events`、`skipped_display_refreshes`、`window_update_request_p95_ms`。
基准产物属于本机诊断文件，不提交 Git。2026-09-09 至 2026-09-10 的同机对比中，
Chart parse 从 195 ms 降至 31-35 ms；通过样本为 59.89-59.99 FPS、canvas paint
p95 2.786-3.288 ms、MainWindow update p95 6.002-9.670 ms，均为 0 skipped refresh、
0 UI stall。最终交付代码复跑为 59.988 FPS / 3.191 ms / 6.638 ms，工作副本创建 89 ms、
解析 32 ms。单次 Windows 调度毛刺
应保留失败结果并同参数复跑确认，不得只看平均 FPS。

播放启动连续性必须另做零预热回归；常规 3 秒 warm-up 会隐藏首个音频位置回调前的
冻结或跳变：

```powershell
build\Release\CatchChartEditor.exe `
  --benchmark-chart $chartPath `
  --benchmark-fps 0 `
  --benchmark-warmup-ms 0 `
  --benchmark-duration-ms 3000 `
  --benchmark-output artifacts\local_playback_startup.json
```

除常规判定外，检查 `playback.audio_progress_wait_ms`、
`playback.initial_clock_error_abs_ms`、`playback.time_step_ms` 和
`visual.scroll_step_change_pct`。音频后端首个位置样本可以延迟到达，但画面帧在等待期间
必须继续推进，首次样本不得直接重设显示时钟。0.1×、0.25×、1×、2×、10× 至少各跑一次。

## Note 音与播放时钟回归

`audio_playback_timing_tests` 验证本地音频首段不丢失、解码帧粒度不会触发假时钟
重同步、画面尚未确认绘制时音效仍继续，以及播放中跳转不集中补播跳过的 Note。
测试使用临时 WAV 和隔离设置，不写入用户谱面。

```powershell
cmake --build build_clion_msvc --config Debug --target PlaybackTimingTests
ctest --test-dir build_clion_msvc -C Debug -R audio_playback_timing_tests --output-on-failure
```

Windows / Qt 6.10+ 可启用独立诊断工具，记录解码帧 PTS、音频位置、预测时钟、每次
音效提交和系统输出回环中的测试音起点。回环只保存检测时刻，不保存原始系统音频。
该工具不加入默认构建或 CTest，不能用其音调检测结果自动校准 offset。
密集场景可使用 `--interval-ms 20 --hit-file resources/note_sounds/kickwav.wav
--muted --no-loopback`；加上 `--stall-ms 150` 单独检查 GUI 阻塞恢复后的过期音效合并。
JSON 的 `voice_restart_count` 与逐次提交时刻用于区分声部满载和集中补播。

```powershell
cmake -S . -B build_clion_msvc -DCATCHEDITOR_BUILD_AUDIO_PROBE=ON
cmake --build build_clion_msvc --config Debug --target NoteSoundTimingProbe
build_clion_msvc\Debug\NoteSoundTimingProbe.exe --output artifacts\note-normal.json
build_clion_msvc\Debug\NoteSoundTimingProbe.exe --paint-delay-ms 90 --output artifacts\note-paint90.json
build_clion_msvc\Debug\NoteSoundTimingProbe.exe --speed 0.1 --no-loopback --output artifacts\note-slow.json
python tools\perf\note_sound_probe_report.py artifacts\note-normal.json artifacts\note-paint90.json
```

`--streaming` 复现旧的 LowLatencyStreaming 输入配置；`--chart <path> --start-ms <ms>
--no-loopback` 只读解析现有谱面及其音乐。工具读取真实谱面时只检查时间链路，不能
通过音乐与测试音的混合检测来宣称真实声音已精确对齐。

`submission_lateness_wall_ms` 只表示相对预测时钟的提交误差，不能等同于听感延迟。
完整原因与证据见 [Note 音时序排查](docs/NOTE_SOUND_TIMING_INVESTIGATION.md)。

## 音频导入与整曲测时回归

`audio_bpm_detector_file_tests` 真实执行异步 WAV 转 OGG，再以新建谱面的整曲时长
请求调用异步 BPM 检测；同时验证不同采样率、区间裁切和超出 64 位采样位置的
错误返回。旧代码会在第一个整曲请求上触发 `qnumeric.h:516` 断言。

```powershell
cmake --build build_clion_msvc --config Debug --target BpmDetectorFileTests
ctest --test-dir build_clion_msvc -C Debug -R audio_bpm_detector_file_tests --output-on-failure
```

默认仅使用临时文件。可设置 `CCE_BPM_IMPORT_AUDIO` 为本机已有音乐路径，附加只读
整曲检测；该文件不会被转换、覆盖或修改。原因追踪见
[音频导入崩溃排查](docs/AUDIO_IMPORT_CRASH_INVESTIGATION.md)。

## 拖动吸附与资源复制回归

`ui_grid_drag_tests` 验证放置吸附与拖动吸附的独立组合、单个/整组选中音符拖动、
组内非网格间距的保留、单步撤销及两个网格设置入口。
`file_resource_copy_tests` 验证已有目标音频不误报、缺失资源仍告警、Unicode 音频
播放副本的创建/重载/释放，以及正式保存时复制工作目录中的背景和音效。
可选设置 `CCE_RESOURCE_COPY_AUDIO` 指向已有 Unicode 路径音乐，附加只读播放验证。

```powershell
cmake --build build_clion_msvc --config Debug --target GridDragTests ResourceCopyTests SettingsTransferTests
ctest --test-dir build_clion_msvc -C Debug -R "ui_grid_drag_tests|file_resource_copy_tests|utils_settings_transfer_tests" --output-on-failure
```

## D Catch 导出插件

Windows 默认构建 D 插件，需先安装 LDC 并将 `ldc2` 加入 PATH。
`CtbProcessPluginTests` 使用真实清单加载器和异步进程适配器，验证动作发现、
读取最新工作副本、将结果保存在原谱面旁、重复导出避让文件名及失败返回。
`ctb_converter_smoke` 验证 `.mc`/`.mcz`、BPM 变化、Rain、offset、Unicode/BOM、
多难度资源重名、默认保留重叠音符、源文件不变及协议错误恢复。
全部写入临时目录，不修改用户谱面。

```powershell
cmake --build build --config Release --target CtbProcessPluginTests
ctest --test-dir build -C Release -R "ctb_converter_smoke|CtbProcessPluginTests" --output-on-failure
```

## 手工回归清单

自动化测试不替代以下真实交互：

- 打开、新建、切换难度、保存、另存和导出 `.mcz`；
- 播放/暂停、变速、滚轮导航、缩放和拖动 seek；
- Note/Rain 放置、框选、范围选择、复制粘贴、镜像和撤销重做；
- 在压力谱面中分别拖动 1 / 100 / 4,000 个 Note 和 Rain 尾部，确认拖动期间顺滑、预览位置正确、松手只增加一个 Undo 项，撤销后谱面完全恢复；
- 默认粘贴优先保持原分母；无法精确表达时允许自动约分；仅在手动启用时把预览和最终 Note 统一量化为 `/288`；
- 曲线锚点/控制柄拖动、连接、段密度、整曲线/目标段提交、样式导入导出和 sidecar 重开；
- 大谱面启用曲线工具后持续移动鼠标，确认 hover 不随 Note 数量明显卡顿，并验证主窗口级 `Delete` 可删除锚点/曲线段；
- Note Color Formatter 在 `Plugin Tools` GUI 中分别验证已选 Note、拍点范围和整谱模式，确认顶部按钮聚焦面板、Sound Note 不变且单次撤销可完整恢复；
- Note Input、Timing & Grid、Playback Speed、Range Select、Mirror Flip、Curve Tools 和 Plugin Tools 分别验证纵向组合、拆分、拖动、浮动、关闭恢复和默认布局重置，并确认拖回后不变为切换标签；
- 在新旧两种布局中验证 Playback Speed 的六个快捷按钮与 `0.1×–10×` 手动输入，确认输入边界、Playback 菜单双向同步、播放中切速和重启持久化；
- 关闭 `View -> Enable Floating Windows`，确认界面恢复为引入 ADS 前的固定 Navigation / Preview / Workspace / 右编辑栏四栏布局，Note/BPM/Meta 只在同一右栏切换，没有任何 ADS 标题、标签或工具拆分块；右栏滚轮及纵向滚动条有效且始终没有横向滚动条；
- 先分别设置经典模式的四栏宽度、Note/BPM/Meta 页和 Plugin Tools 开关，以及多窗口模式的停靠/浮窗/关闭状态；反复切换并重启，确认每种模式只恢复自己的最后状态，互不手动同步或覆盖；
- 多窗口模式尝试关闭 `Chart Workspace`，确认主编辑区没有关闭入口；关闭其他面板后，从 `View -> Panels`、对应工具页和 `Reopen All Closed Panels` 恢复；顶栏仅显示工具页切换；
- 依次关闭 Timing、Playback Speed、Range、Mirror、Curve、Plugin Tools 或 Chart Statistics，确认相邻紧凑模块不被拉高；剩余空间由普通编辑面板吸收，纯工具浮窗中则表现为空白；
- Navigation、Preview、Note、BPM、Meta 及插件面板的停靠、拆分、浮动、关闭、无需重启恢复和重启持久化；BPM/Meta 等完整面板另验证标签组合；
- 关闭主窗口后确认 GUI、日志终端、外部插件子进程和主进程均结束；
- 深色/浅色主题下主窗口与浮动窗口标题栏、文字、边框和首帧内容；
- 深色/浅色主题下分别检查范围拍点、Time Division、镜像轴和 Plugin Tools 范围输入，确认输入框、下拉框和数值框背景与文字对比度正确；
- 调整右侧栏和浮动工具窗口宽度，确认动作按钮保持内容尺寸，输入框与下拉框仍能自适应且不截断主要内容；
- 外部进程插件启动失败、超时、重新加载和 Python 不在 PATH 的提示；
- 中文、英文、日文界面的关键菜单和对话框。

## 新增测试原则

- 修复回归时优先添加能在旧代码上失败的用例。
- 模型/I/O/Note Chain 默认加入 `minimal_tests.cpp`；ADS 与顶层窗口行为加入 `docking_layout_tests.cpp`。
- 正常路径和失败/边界路径至少各覆盖一个。
- 文件写入使用临时目录；不得依赖开发机固定路径或用户数据。
- 多显示器断言允许系统窗口边框误差，但不能把合法负坐标钳制到 `(0,0)`。
- 浮点换算使用明确误差，不直接比较非整数结果。
- 测试可以推进事件循环；生产拖动/绘制路径不得用嵌套 `processEvents()` 掩盖时序问题。

## 常见失败

- Windows 本机的 MSVC 14.51.36231 曾在 Debug 的 `<functional>`、UCRT 等不同位置报 `C1001`，也曾直接退出编译进程；14.44.35207 已通过本项目 Debug/Release 验证。若已安装该工具集，可用下面的命令重建 CMake 配置，再执行正常编译。`--fresh` 保留源码和输出目录，只重新生成构建配置；本地 VS Code 的 `cmake.toolset` 及 CMake preset 的 `toolset` 也应同步指定 `v143,version=14.44.35207`，避免 IDE 又选回默认工具集。

  ```powershell
  cmake --fresh -S . -B build -G "Visual Studio 18 2026" -A x64 -T "v143,version=14.44.35207" -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 -DBUILD_TESTING=ON
  cmake --build build --config Debug --target CatchChartEditor --parallel 2
  ```

- `0xc0000135`：通常是 Qt DLL 不在测试进程 PATH；先确认 CMake 配置使用了正确 Qt。
- 本机在 14.44 下也出现过不同旧文件中的随机 `C1001` / `0xc0000005`。2026-10-18 的相关 Debug 目标在仅限制构建子进程 CPU 亲和性、逐文件串行编译后通过；没有修改系统全局设置，也没有据报错位置改写业务代码。这只是本轮构建规避，不能据此确定工具链或硬件根因。构建记录见 `artifacts/spectrum-blocks-debug-build-affinity.log`。
- UI 测试输出 `QWindowsWindow::setGeometry ... +0+0`：检查预览到真实浮动窗口是否丢失全局坐标。
- 测试无输出但返回非零：直接运行对应可执行程序查看 stderr，例如 `build\Debug\DockingLayoutTests.exe`。
- 只在 Release/Debug 失败：分别清查未初始化状态、断言、副作用时序和优化相关未定义行为。
