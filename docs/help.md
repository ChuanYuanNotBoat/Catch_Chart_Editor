# Malody Catch Editor 帮助文档

> 适用版本：Beta v1.11.0 开发周期
> 其他文档：[文档总索引](README.md)

本文按界面中的功能位置说明用途、使用方法和默认快捷键。第一次使用时，建议按“快速上手”走一遍，再按菜单查找具体功能。

## 快速上手

1. 从 `File -> Open Chart...` 打开 `.mc` 谱面，或从 `File -> Open Folder...` / `Open Imported Charts...` 选择谱面。
2. 在右侧工具栏选择 `Note` 面板，选择 `Place Note` 后在中央画布左键放置音符。
3. 用鼠标滚轮上下浏览谱面；按住 `Ctrl` 滚轮缩放时间轴；左侧 `Zoom` 也可以调整纵向缩放。
4. 按 `Space` 播放/暂停，或使用左侧 `Play` 按钮。
5. 需要精确放置时，在右侧 `Time Division` 选择拍线分度，并开启 `Grid Snap`。
6. 编辑完成后用 `File -> Save` 保存 `.mc`，或用 `File -> Export .mcz...` 导出 Malody 谱面包。

## 主程序文档

### 主界面区域

- 顶部菜单栏：包含文件、编辑、视图、设置、播放、工具、插件和帮助入口。
- 顶部工具栏：`Note` / `BPM` / `Meta` 打开并聚焦对应编辑面板；`Curve` 启动或关闭原生曲线编辑工具；`Quantize Paste to 1/288` 显示并切换粘贴颜色模式；`Plugins` 打开插件管理器。
- `Navigation` 面板：显示谱面密度曲线、播放按钮、纵向缩放，以及外部插件提供的快捷按钮。
- `Realtime Preview` 面板：实时预览当前谱面效果。
- `Chart Workspace`：不可关闭的中央谱面画布与时间密度导航条。
- `Note Input`、`Timing & Grid`、`Range Select`、`Mirror Flip`、`Curve Tools`、`Plugin Tools`：按功能组成中等粒度工具块；`BPM & Timing` 和 `Metadata` 保持独立面板。

### 可组合面板布局

- 拖动普通面板标题或标签可以改变停靠位置；BPM、Meta 等完整面板仍可组合成标签页。
- 将面板拖离主窗口即可变成独立浮动窗口；浮动面板仍可拖回主窗口或与其他浮动面板组合。
- Note 输入、时间网格、范围、镜像、曲线和插件工具停靠时按原右侧栏顺序纵向展开，同时可见且不使用切换标签；只有拖离停靠区后才成为独立窗口。
- 将工具块拖回其他工具块的边缘会恢复 splitter 组合，不会变成只能显示其中一个的切换标签。Curve Tools 和 Plugin Tools 默认关闭，分别由 Curve 开关和插件快捷按钮打开。
- 工具块停靠在右侧栏时不显示完整窗口标题栏，只在右上角保留 `⠿` 小手柄；拖动手柄可将该工具块移出为带原生标题栏的浮动窗口。各块之间不绘制额外分隔线和外框，仍使用原侧栏的连续背景与 GroupBox 层级。
- 面板关闭后可立即从 `View -> Panels` 或顶部 `Note` / `BPM` / `Meta` 按钮重新打开，无需重启程序。
- `View -> Panels -> Reset Panel Layout`：恢复默认工作区、导航、预览和编辑器布局。
- 主窗口大小、面板停靠关系、标签组合和浮动位置会在正常退出时保存，并在下次启动时恢复。
- 长面板内容会在面板内部滚动，不再强制增大主窗口的最小高度。

### File 文件菜单

- `File -> Open Chart...`：打开单个 `.mc` 或 `.mcz` 谱面。快捷键：`Ctrl+O`。
- `File -> Open Folder...`：打开一个文件夹并从其中选择 `.mc` 谱面，适合一个歌曲文件夹内有多个难度时使用。
- `File -> Open Imported Charts...`：打开本地已导入谱面库。快捷键：`Ctrl+Shift+O`。
- `File -> Save`：保存当前谱面到原始 `.mc` 文件。快捷键：`Ctrl+S`。
- `File -> Save As...`：另存为新的 `.mc` 文件。
- `File -> Export .mcz...`：导出 Malody 可导入的 `.mcz` 谱面包，会打包谱面、音频、背景等必要资源。
- `File -> Switch Difficulty...`：在同一目录或已导入歌曲中切换其他难度谱面。
- `File -> Exit`：退出程序。快捷键：`Ctrl+Q`。关闭主窗口同样会结束整个程序，包括隐藏的浮动面板和日志终端进程。

### Edit 编辑菜单

- `Edit -> Undo`：撤销上一步编辑。快捷键：`Ctrl+Z`。
- `Edit -> Redo`：重做撤销的编辑。快捷键：`Ctrl+Y`。
- `Edit -> Copy`：复制当前选中的音符。快捷键：`Ctrl+C`。如果没有选中音符，第一次按下会记录参考线处的区间起点，移动视图后再次按下会复制区间内音符。
- `Edit -> Paste`：进入粘贴预览。快捷键：`Ctrl+V`。拖动预览可调整位置，点击画布左上角 `Confirm` 确认，点击 `Cancel` 或按 `Esc` 取消。
- `Edit -> Delete`：删除当前选中的音符或插件工具中的选中对象。快捷键：`Delete`。
- `Edit -> Quantize Paste to 1/288`：手动开启后，将粘贴的普通/Rain 音符起点和 Rain 终点舍入到 `1/288` 并以分母 `288` 保存，使粘贴后的 Note 统一使用 `/288` 蓝色。默认关闭；关闭时优先保留各 Note 原分母，原分母无法精确表达新拍点时才自动约分。

### View 视图菜单

- `View -> Panels`：显示或隐藏导航、预览、Note 输入、时间网格、范围、镜像、曲线、插件、BPM 和 Meta 面板，也可恢复默认面板布局。
- `View -> Color Notes`：按音符拍型/分度给音符上色，便于检查节奏密度。
- `View -> Color Timeline Divisions`：按时间轴分度给网格线着色。
- `View -> Timeline Division Color Advanced Settings...`：设置分度线颜色规则。可选择 `Classic`、`All` 或自定义常见分度/额外分度。
- `View -> Hyperfruit Outline`：显示红果/高密度音符的描边提示。
- `View -> Vertical Flip`：切换谱面下落方向/垂直显示方向。
- `View -> Show Background Image`：显示或隐藏谱面背景图。
- `View -> Background Color`：设置画布背景色，包含 `Black`、`White`、`Gray` 和 `Custom...`。

### Settings 设置菜单

- `Settings -> Note Size...`：调整无皮肤或回退绘制时的音符大小。加载皮肤时，音符大小主要由皮肤校准控制。
- `Settings -> Calibrate Skin...`：校准当前皮肤缩放，让皮肤素材与编辑画布对齐。
- `Settings -> Outline Settings...`：设置音符描边宽度与颜色。
- `Settings -> Note Sound Volume...`：调整编辑时音符音效音量。
- `Settings -> Session Settings...`：设置编辑会话选项，包括自动保存间隔和音频校正测试开关。
- `Settings -> Skin`：选择可用皮肤。皮肤来自程序目录的 `skins` 或内置默认皮肤资源。
- `Settings -> Note Sound`：选择编辑音符时播放的按键音；选择 `None` 可关闭音效。
- `Settings -> Keyboard Shortcuts...`：自定义可配置快捷键。清空输入框可禁用对应快捷键，`Reset` 恢复单项默认值，`Reset All` 恢复全部默认值。当前更稳定支持 `Ctrl` / `Shift` 参与的双键组合。
- `Settings -> Language`：切换界面语言。

### Playback 播放菜单

- `Playback -> Play/Pause`：播放或暂停当前谱面音频。快捷键：`Space`。
- `Playback -> Speed`：选择播放速度：`0.25x`、`0.5x`、`0.75x`、`1.0x`。

播放时画布会跟随播放头自动滚动；手动滚动或拖动时间位置后会暂时关闭自动滚动。

### Tools 工具菜单

- `Tools -> Grid Settings...`：设置横向网格吸附，包含是否启用吸附和每行网格数量，范围为 `4-64`。
- `Tools -> Log Settings...`：设置日志输出，包括 JSON 日志、详细日志等。
- `Tools -> Export Diagnostics Report...`：导出诊断报告，便于反馈问题或分析性能。

### Help 帮助菜单

- `Help -> Check for Updates...`：检查 GitHub 发布页是否有新版本。
- `Help -> Help Documentation...`：打开本文档。
- `Help -> About...`：查看程序信息。
- `Help -> Version Information...`：查看版本信息与更新说明。
- `Help -> Logs...`：查看日志列表，支持刷新、打开选中日志、打开当前日志和打开日志文件夹。

### Note 工具面板

以下功能块停靠时组成可调整高度的纵向工具栏，也可以独立拆分、拖动和浮动：

- `Note Input`：放置、删除、选择、复制以及插件提供的 Note 放置动作。
- `Timing & Grid`：`Time Division`、横向网格吸附和网格设置。
- `Range Select`：大范围拍点输入、当前时间填入、范围覆盖层和范围选择。
- `Mirror Flip`：镜像轴、参考线、预览和执行翻转。
- `Curve Tools`：曲线锚点、选择目标、连接、提交和重置；启用顶部 `Curve` 时自动打开。
- `Plugin Tools`：由插件动作元数据生成的宿主 GUI；原生插件面板默认加入同一纵向工具区。

工具块中的拍点输入、下拉选择和数值输入会统一跟随明暗主题；暗色模式下不会使用系统默认白色输入背景。

- `Place Note`：普通音符放置模式。左键空白处放置音符；左键已有音符可选中并拖动。
- `Place Rain`：雨音符放置模式。第一次左键设置起点，第二次左键设置终点；终点必须晚于起点。
- `Delete Mode`：删除模式。左键已有音符会删除该音符。
- `Select Mode`：选择模式。左键点击选择音符，拖拽框选多个音符。
- `Place Anchor`：启动原生曲线工具并进入锚点放置模式。
- `Copy`：与 `Edit -> Copy` 相同。
- `Time Division`：设置时间分度，影响音符放置、播放头吸附、粘贴预览和曲线生成密度。可手动输入，最大会限制到 `96`。
- `Grid Snap`：开启后横向位置吸附到网格。
- `Grid Settings...`：设置横向网格数量。
- `Mirror Flip`：按指定 `Axis X` 镜像翻转选中音符。`Show Guide` 显示可拖动参考线，`Show Preview` 显示翻转预览，`Flip Selected` 执行翻转。
- 原生曲线控制：启用 `Curve` 或 `Place Anchor` 后出现，包含锚点放置、曲线显示、折线模式、音符吸附、选择目标、提交、连接、断开、删除和重置。

### BPM & Timing 面板

- BPM 列表：显示当前谱面所有 BPM 点，格式为 `小节:分子/分母  BPM`。
- `Time`：输入 BPM 点位置，例如 `0:1/1`。
- `BPM`：输入 BPM 数值。
- `Add/Update`：未选中列表项时添加 BPM；选中列表项时更新该 BPM。
- `Remove`：删除选中的 BPM。

### Metadata 面板

用于编辑谱面元信息。字段修改后会自动保存到当前会话，也可以点击 `Save` 手动保存。

- `Title` / `Original Title`：标题与原始标题。
- `Artist` / `Original Artist`：曲师与原始曲师名。
- `Difficulty`：难度名。
- `Chart Author`：谱师。
- `Audio (ogg)`：选择或填写音频文件路径。
- `Background (jpg)`：选择或填写背景图路径。
- `Preview Time`：试听预览时间，单位毫秒。
- `First BPM`：首个 BPM。
- `Offset`：音频偏移，单位毫秒。
- `Fall Speed`：谱面下落速度。

### 画布鼠标与键盘操作

- 鼠标滚轮：上下滚动谱面。
- `Ctrl + 鼠标滚轮`：缩放时间轴。
- 左键空白处：在 `Place Note` 模式下放置普通音符。
- 左键已有音符：选中并拖动音符。
- `Ctrl + 左键音符`：切换该音符的选中状态。
- `Ctrl + 左键拖拽`：框选音符。
- 右键画布：打开上下文菜单。
- 右键菜单 `Play from Reference Time`：从参考线时间开始播放。
- 右键菜单 `Paste`：在鼠标位置粘贴剪贴板音符。
- 右键菜单 `Quantize Paste to 1/288`：与顶部工具栏、`Edit` 菜单中的同名开关同步；粘贴预览左上角会显示当前 `Timing: 1/288` 状态。
- 右键菜单 `Mirror Flip Selected (Center Line)`：按默认中心线镜像翻转当前目标音符。
- 右键菜单 `Edit Color (By Division)`：批量设置目标音符的颜色分度，也可选择 `Minimal Irregular (Red)` 标记最小非常规分度。
- `Esc`：取消粘贴预览或区间复制状态。
- `Delete`：删除选中音符。

## 曲线工具与插件文档

Note Chain Assist 已由主程序内部的 C++ 模块实现，不依赖 Python 或插件进程。其他外部插件通常放在程序目录的 `plugins` 文件夹下，并需要对应的 `*.plugin.json` 描述文件和脚本/二进制文件。

### Plugins 插件菜单

- `Plugins -> Plugin Manager...`：打开插件管理器。可查看插件启用状态、加载状态、名称、ID、版本、作者和能力；勾选 `Enabled` 后点击 `Reload Plugins` 应用；`Open Plugins Folder` 可打开插件目录。
- `Plugins -> Plugin Actions`：显示外部插件提供的菜单动作，例如颜色格式化。
- `Plugins -> Plugin Panels`：打开通用 `Plugin Tools` 或插件提供的原生 ADS 面板。两者都可与内置工具组合、拆分或浮动。
- `Plugins -> Curve Edit Tool`：启动或关闭原生曲线编辑工具，与顶部工具栏的 `Curve` 状态同步。
- `Plugins -> Export Curve Style...` / `Import Curve Style...`：导出或导入原生曲线的密度样式预设。
- `Plugins -> Plugin Overlay Elements`：控制插件叠加层显示内容，包括 `Enable Overlay`、`Preview Notes`、`Control Points`、`Handles`、`Sample Points`、`Labels`。

### 插件工具栏与侧栏入口

- 顶部主工具栏：`Curve` 启动/关闭原生曲线工具；`Plugins` 按钮打开插件管理器；带范围选择器的插件快捷动作会打开并聚焦 `Plugin Tools` GUI。
- 左侧 `Plugin Shortcuts`：显示外部插件提供的快捷按钮。不同插件会按分组显示，点击按钮执行对应动作。
- 右侧 `Note` 面板：原生曲线工具启用时显示曲线控制项。

### 原生曲线工具：Note Chain Assist

用途：在主画布上编辑曲线锚点与控制柄，再按指定密度生成一串真实的 Catch 普通音符。该工具完全运行在主程序内部，旧的 `builtin.note_chain_assist` Python 插件只保留兼容文件，不会再启动 Python 进程。

位置：

- 顶部主工具栏：`Curve`。
- `Plugins -> Curve Edit Tool`。
- 右侧 `Note -> Place Anchor`。
- 右侧 `Note` 面板中的原生曲线控制。
- 原生曲线工具启用时的画布右键菜单。
- `Plugins -> Export Curve Style...` / `Import Curve Style...`。

常用动作：

- `Commit Curve → Notes`：按每一段的密度将整条曲线提交为真实普通音符；已有同拍同位置音符不会重复生成。快捷键：曲线工具启用时按 `Enter`。
- `Commit Context Segments -> Notes`：在曲线段上右键，只提交右键命中的目标段；框选或已选择的段也会纳入右键目标。
- `Anchor Placement`：开启后左键空白处添加锚点；关闭时切换到选择模式。快捷键：曲线工具启用时按 `A`。
- `Show Curve`：显示或隐藏曲线、锚点及控制柄。
- `Polyline Mode`：在贝塞尔曲线和直线段之间切换；也可在目标段右键选择 `Toggle Curve / Polyline`。
- `Snap Notes to Curve`：拖动普通音符或粘贴预览时，在对应拍点将横向位置吸附到曲线；开启后自动进入选择模式。
- `Select: Anchors` / `Select: Segments` / `Select: Notes`：控制选择模式和框选能够命中的对象。选择音符时会按拍点和位置同步匹配最近且尚未使用的锚点。
- `Connect Selected` / `Disconnect Selected`：连接选中的锚点，或断开选中的曲线段。
- `Delete Selected`：删除选中的锚点或曲线段。`Reset Curve` 清空当前曲线工程。
- `Generated Note Spacing`：在曲线段右键菜单中设置提交后生成 Note 的拍点间隔。`Follow Time Division` 跟随右侧当前分度，也可固定为样式提供的 `1/n`；画布顶部只显示当前实际生效或已选曲线段的间隔，不再显示含义不清的可用分度列表。
- `Export Curve Style...` / `Import Curve Style...`：导出或导入密度分母列表等曲线样式设置。

画布操作：

- 左键拖动锚点或控制柄：移动节点/柄；锚点始终按拍点顺序组织。
- 开启 `Anchor Placement` 后左键空白处：在对应拍点插入锚点，并自动推断默认控制柄。
- 右键锚点：删除锚点。
- 双击锚点：切换平滑/折角状态。
- `Shift + 拖动锚点到另一个锚点`：连接节点。
- 选择模式下拖拽空白处：框选已启用的锚点、曲线段和音符目标。
- `Esc`：取消当前拖拽、连接或框选并清除曲线选择。
- `Delete` / `Backspace`：删除当前曲线选择；即使焦点位于主窗口或侧栏，主窗口级删除动作也会优先转发给已选锚点/曲线段。
- `Ctrl+Z` / `Ctrl+Y`：通过主程序统一撤销时间线撤销/重做曲线编辑和音符提交。

说明：

- 曲线工具只生成和吸附普通 note；雨音符等特殊类型不作为曲线修改对象。
- 曲线数据使用 V3 JSON sidecar，存放在谱面工作副本旁的 `.mcce-plugin/*.curve_tbd.json` 中；保存谱面时会与源谱面同步，切换谱面时会事务式切换工程。
- V3 sidecar 会保留曲线/节点身份、分组、连接、控制柄、形状、密度模式及扩展元数据，并兼容读取旧 Python 插件的 `anchors`、handle 和 `links` 数据。
- 曲线预览本身不会修改谱面；只有执行整曲线或目标段提交后才会生成真实音符。

### BPM 辅助文件

BPM 辅助文件用于存储 BPM 测量工具相关的元数据，包括 BPM 排除时间段和歌曲 BPM 信息。

文件位置：
- BPM 排除文件：`.mcce-plugin/{谱面文件名}.bpm_excludes.json`
- 歌曲 BPM 文件：`.mcce-plugin/{谱面文件名}.song_bpm.json`

这些文件会在保存、复制、导出/导入 MCZ 时自动同步，确保 BPM 相关数据不会丢失。

### ChartFileSystem 注册表

ChartFileSystem 是一个集中式文件类型管理系统，用于 MCZ 打包时决定哪些文件应该被包含。它替代了之前的硬编码白名单，支持运行时动态注册/注销文件类型。

内置文件类型包括：
- `.mc`：谱面文件
- 音频格式：`.ogg`, `.mp3`, `.wav`, `.flac`, `.m4a`, `.aac`
- 图片格式：`.jpg`, `.jpeg`, `.png`, `.bmp`, `.webp`, `.gif`
- 视频格式：`.mp4`, `.mkv`, `.avi`, `.webm`, `.mov`
- Sidecar 文件：`.curve_tbd.json`, `.bpm_excludes.json`, `.song_bpm.json`

### 内置插件：Note Color Formatter

用途：整理音符颜色分度字段，让按分度上色与颜色分组逻辑更统一。适合导入旧谱、批量编辑后快速清理颜色分度。

位置：

- 顶部插件工具栏：`Format Note Colors` 快捷按钮。
- `View -> Panels -> Plugin Tools` / `Plugins -> Plugin Panels -> Plugin Tools`。
- `Plugins -> Plugin Actions`。

使用方法：

1. 打开谱面。
2. 点击顶部 `Format Note Colors`，打开并聚焦可停靠的 `Plugin Tools` 面板。
3. 在常驻 GUI 中选择 `Selected Notes`、`Beat Range` 或 `Entire Chart`；选中范围会实时显示数量。
4. 选择拍点范围时，按大范围选择器相同的 `整数 分子/分母` 格式填写起止点，也可用 `Now` 读取当前播放时间。
5. 点击面板内的 `Format Note Colors` 执行。只格式化普通和 Rain 音符的起点颜色分度，不修改 Sound Note；整次修改作为一个可撤销动作写入主程序历史。

`Plugins -> Plugin Actions` 仍保留一次性范围对话框，适合不希望常驻面板时使用。

### 插件使用注意

- 如果插件菜单为空，先到 `Plugins -> Plugin Manager...` 检查插件是否启用、是否加载成功。
- 修改插件启用状态后，需要点击 `Reload Plugins` 才会应用。
- 外部进程插件仍依赖对应运行环境，例如 Python 插件需要系统 PATH 中能找到 `python`；原生 Note Chain Assist 不受此限制。
- 插件动作可能会批量修改谱面，执行前建议先保存或确认自动保存设置。

## 默认快捷键速查

| 功能 | 默认快捷键 | 位置 |
| --- | --- | --- |
| 打开谱面 | `Ctrl+O` | `File -> Open Chart...` |
| 打开已导入谱面 | `Ctrl+Shift+O` | `File -> Open Imported Charts...` |
| 保存 | `Ctrl+S` | `File -> Save` |
| 退出 | `Ctrl+Q` | `File -> Exit` |
| 撤销 | `Ctrl+Z` | `Edit -> Undo` |
| 重做 | `Ctrl+Y` | `Edit -> Redo` |
| 复制 | `Ctrl+C` | `Edit -> Copy` / 右侧 `Note -> Copy` |
| 粘贴 | `Ctrl+V` | `Edit -> Paste` |
| 删除 | `Delete` | `Edit -> Delete` |
| 播放/暂停 | `Space` | `Playback -> Play/Pause` |
| 取消当前操作 | `Esc` | 画布 |
| 缩放时间轴 | `Ctrl + 鼠标滚轮` | 画布 |
| 切换曲线锚点放置 | `A` | 原生 Note Chain Assist 工具模式 |
| 提交整条曲线 | `Enter` | 原生 Note Chain Assist 工具模式 |
