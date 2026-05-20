# Malody Catch Editor 帮助文档

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
- 顶部工具栏：`Note` / `BPM` / `Meta` 切换右侧编辑面板；`Plugins` 打开插件管理器；`Launch Curve Tool` 启动曲线插件工具模式。
- 左侧栏：显示谱面密度曲线、播放按钮、纵向缩放，以及插件快捷按钮。
- 中央预览与画布：中间为主编辑画布，旁边实时预览当前谱面效果。
- 右侧栏：按工具栏切换 `Note`、`BPM`、`Meta` 三个编辑面板。

### File 文件菜单

- `File -> Open Chart...`：打开单个 `.mc` 或 `.mcz` 谱面。快捷键：`Ctrl+O`。
- `File -> Open Folder...`：打开一个文件夹并从其中选择 `.mc` 谱面，适合一个歌曲文件夹内有多个难度时使用。
- `File -> Open Imported Charts...`：打开本地已导入谱面库。快捷键：`Ctrl+Shift+O`。
- `File -> Save`：保存当前谱面到原始 `.mc` 文件。快捷键：`Ctrl+S`。
- `File -> Save As...`：另存为新的 `.mc` 文件。
- `File -> Export .mcz...`：导出 Malody 可导入的 `.mcz` 谱面包，会打包谱面、音频、背景等必要资源。
- `File -> Switch Difficulty...`：在同一目录或已导入歌曲中切换其他难度谱面。
- `File -> Exit`：退出程序。快捷键：`Ctrl+Q`。

### Edit 编辑菜单

- `Edit -> Undo`：撤销上一步编辑。快捷键：`Ctrl+Z`。
- `Edit -> Redo`：重做撤销的编辑。快捷键：`Ctrl+Y`。
- `Edit -> Copy`：复制当前选中的音符。快捷键：`Ctrl+C`。如果没有选中音符，第一次按下会记录参考线处的区间起点，移动视图后再次按下会复制区间内音符。
- `Edit -> Paste`：进入粘贴预览。快捷键：`Ctrl+V`。拖动预览可调整位置，点击画布左上角 `Confirm` 确认，点击 `Cancel` 或按 `Esc` 取消。
- `Edit -> Delete`：删除当前选中的音符或插件工具中的选中对象。快捷键：`Delete`。
- `Edit -> Paste with 288 Division`：切换粘贴时是否使用 288 分度转换，以将粘贴note统一为蓝色。

### View 视图菜单

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

### 右侧 Note 面板

- `Place Note`：普通音符放置模式。左键空白处放置音符；左键已有音符可选中并拖动。
- `Place Rain`：雨音符放置模式。第一次左键设置起点，第二次左键设置终点；终点必须晚于起点。
- `Delete Mode`：删除模式。左键已有音符会删除该音符。
- `Select Mode`：选择模式。左键点击选择音符，拖拽框选多个音符。
- `Place Anchor`：进入曲线插件的锚点放置模式；需要有支持画布交互的插件。
- `Copy`：与 `Edit -> Copy` 相同。
- `Time Division`：设置时间分度，影响音符放置、播放头吸附、粘贴预览和曲线生成密度。可手动输入，最大会限制到 `96`。
- `Grid Snap`：开启后横向位置吸附到网格。
- `Grid Settings...`：设置横向网格数量。
- `Mirror Flip`：按指定 `Axis X` 镜像翻转选中音符。`Show Guide` 显示可拖动参考线，`Show Preview` 显示翻转预览，`Flip Selected` 执行翻转。
- `Curve Plugin Options`：当曲线插件可用时出现，用于显示插件提供的放置相关选项。

### 右侧 BPM 面板

- BPM 列表：显示当前谱面所有 BPM 点，格式为 `小节:分子/分母  BPM`。
- `Time`：输入 BPM 点位置，例如 `0:1/1`。
- `BPM`：输入 BPM 数值。
- `Add/Update`：未选中列表项时添加 BPM；选中列表项时更新该 BPM。
- `Remove`：删除选中的 BPM。

### 右侧 Meta 面板

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
- 右键菜单 `Mirror Flip Selected (Center Line)`：按默认中心线镜像翻转当前目标音符。
- 右键菜单 `Edit Color (By Division)`：批量设置目标音符的颜色分度，也可选择 `Minimal Irregular (Red)` 标记最小非常规分度。
- `Esc`：取消粘贴预览或区间复制状态。
- `Delete`：删除选中音符。

## 插件文档

插件功能与主程序分段说明，但入口仍在同一个程序内。内置插件会随程序一起加载；外部插件通常放在程序目录的 `plugins` 文件夹下，并需要对应的 `*.plugin.json` 描述文件和脚本/二进制文件。

### Plugins 插件菜单

- `Plugins -> Plugin Manager...`：打开插件管理器。可查看插件启用状态、加载状态、名称、ID、版本、作者和能力；勾选 `Enabled` 后点击 `Reload Plugins` 应用；`Open Plugins Folder` 可打开插件目录。
- `Plugins -> Plugin Actions`：显示插件提供的菜单动作，例如颜色格式化、导入/导出曲线样式等。
- `Plugins -> Plugin Panels`：打开插件提供的面板。如果当前没有插件面板，会显示空项。
- `Plugins -> Plugin Enhanced Tool Mode`：启用插件增强画布工具模式。启用后，支持画布交互的插件可以接管左键、右键、滚轮、键盘等输入。
- `Plugins -> Plugin Overlay Elements`：控制插件叠加层显示内容，包括 `Enable Overlay`、`Preview Notes`、`Control Points`、`Handles`、`Sample Points`、`Labels`。

### 插件工具栏与侧栏入口

- 顶部 `Plugins` 工具栏：`Plugins` 按钮打开插件管理器；`Launch Curve Tool` 启动/关闭曲线工具模式。
- 左侧 `Plugin Shortcuts`：显示插件提供的快捷按钮。不同插件会按分组显示，点击按钮执行对应动作。
- 右侧 `Note -> Curve Plugin Options`：显示曲线插件提供的可勾选工具选项，例如锚点放置、显示曲线、可选中对象等。

### 内置插件：Note Chain Assist

用途：在主画布上编辑曲线锚点与控制柄，再把曲线转换成一串 Catch 音符。适合快速制作连续 note 串、曲线变形和密度调整。

位置：

- 顶部插件工具栏：`Launch Curve Tool`。
- `Plugins -> Plugin Enhanced Tool Mode`。
- 右侧 `Note -> Place Anchor`。
- 右侧 `Note -> Curve Plugin Options`。
- 左侧 `Plugin Shortcuts` 分组。
- 插件工具模式下的画布右键菜单。
- `Plugins -> Plugin Actions` 中的工具菜单动作。

常用动作：

- `Commit Curve`：将当前曲线提交为真实音符。位置：顶部插件工具栏、左侧插件快捷区、插件右键菜单。快捷键：插件工具模式下按 `Enter`。
- `Anchor Place`：开启后左键空白处添加锚点；关闭后可减少误放置。位置：右侧 `Curve Plugin Options`。快捷键：插件工具模式下按 `A`。
- `Show Curve (with Nodes)`：显示/隐藏曲线与节点叠加层。位置：右侧 `Curve Plugin Options`。
- `Selectable: Anchors` / `Selectable: Segments` / `Selectable: Notes`：控制画布上可选中的对象类型。位置：右侧 `Curve Plugin Options`。
- `Reset Curve`：重置全部曲线锚点与控制柄。位置：左侧插件快捷区。
- `Connect Selected`：连接选中的锚点。位置：左侧插件快捷区、插件右键菜单。
- `Delete Selected Segment`：删除选中的曲线段。位置：左侧插件快捷区、插件右键菜单。
- `Curve Placement Density`：设置曲线生成音符的密度。位置：插件工具模式下右键菜单。可选择跟随编辑器分度，或按具体 `1/n` 密度设置；选中两节点之间的分段时可单独设置分段密度。
- `Cycle Density`：切换曲线密度样式。位置：插件右键菜单。
- `Export Style` / `Import Style`：导出或导入曲线样式预设。位置：`Plugins -> Plugin Actions`。

画布操作：

- 左键拖动锚点或控制柄：移动节点/柄。
- 开启 `Anchor Place` 后左键空白处：追加锚点。
- 右键锚点：删除锚点。
- 双击锚点：切换平滑/折角状态。
- 鼠标滚轮：切换分母序列样式。
- `Shift + 拖动锚点到另一个锚点`：连接节点。
- `Esc`：取消插件当前操作。
- `Ctrl+Z` / `Ctrl+Y`：在插件工具模式下撤销/重做曲线相关编辑。

说明：

- 曲线插件主要处理普通 note；雨音符等特殊类型会显示但不作为曲线修改对象。
- 曲线数据会存放在谱面旁的 `.mcce-plugin/*.curve_tbd.json` sidecar 文件中，并会在源谱面与工作副本之间同步。
- 只有执行 `Commit Curve` 后，曲线预览才会写成真实音符。

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

- 顶部插件工具栏：`Format Note Colors`。
- 左侧 `Plugin Shortcuts`：`Format Note Colors`。
- `Plugins -> Plugin Actions`。

使用方法：

1. 打开谱面。
2. 点击任意位置的 `Format Note Colors`。
3. 插件会批量整理当前谱面的音符颜色分度，并让主程序重新加载或刷新谱面。

### 插件使用注意

- 如果插件菜单为空，先到 `Plugins -> Plugin Manager...` 检查插件是否启用、是否加载成功。
- 修改插件启用状态后，需要点击 `Reload Plugins` 才会应用。
- 进程插件依赖对应运行环境。例如 Python 插件需要系统 PATH 中能找到 `python`。
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
| 切换曲线锚点放置 | `A` | Note Chain Assist 工具模式 |
| 提交曲线 | `Enter` | Note Chain Assist 工具模式 |
