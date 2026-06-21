# Malody Catch Editor — AI 项目指南

> 本文档旨在帮助 AI（及新开发者）快速理解项目结构，避免重复探索代码库浪费 token。
> **维护规则**：当项目结构发生重大变化时，请同步更新本文档。
>
> 最后更新：2026-06-21 | 项目版本：Beta v1.10.2

---

## 1. 项目概览

**Malody Catch Editor** 是一个专为 Malody 节奏游戏 Catch 模式设计的**谱面编辑器**。采用 Qt6/C++17 开发，跨平台支持 Windows / macOS / Linux / Android。

- **仓库**：https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor
- **许可证**：LICENSE 文件（项目根目录）
- **当前版本**：Beta v1.10.2
- **最新提交**：`b760a05`

### 核心功能
- 可视化的 Catch 模式谱面编辑（Note 增删改拖拽）
- 三种音符类型：Normal（普通）、Sound（音效）、Rain（长按）
- BPM 编辑与自动拍点检测
- 音频播放同步与预览
- 插件系统（原生 DLL + 外部进程两种形态）
- 撤销/重做支持
- MCZ 打包文件读写

---

## 2. 技术栈

| 层面 | 技术 |
|------|------|
| **语言** | C++17 |
| **UI框架** | Qt 6 (Widgets 模块为主，含 Multimedia、Svg) |
| **构建系统** | CMake 3.21+ (CMakeLists.txt 在项目根目录) |
| **编译器** | Windows: MSVC 2022；macOS/Linux: Clang/GCC |
| **CI/CD** | GitHub Actions (.github/workflows/) |
| **音频** | QMediaPlayer (Qt Multimedia) |
| **代码风格** | 遵循项目现有 CamelCase 命名，Tab 缩进 |

### Qt6 依赖模块
- `Qt6::Widgets` — UI 组件
- `Qt6::Multimedia` — 音频播放
- `Qt6::Svg` — SVG 图标渲染

---

## 3. 构建与运行

### 依赖
- **CMake** >= 3.21
- **Qt 6.x**（Widgets, Multimedia, Svg 模块）
- **C++17 编译器**（MSVC 2022 / Clang / GCC）

### 构建命令
```bash
# 配置（Windows MSVC 示例）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build

# 运行
./build/Malody_Catch_Editor
```

### Android 构建
```bash
# 配置 Android 工具链后
cmake -B build_android -G "Ninja" \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24
cmake --build build_android
```

> 相关脚本参考 `tools/` 目录和 `CMakeLists.txt` 中的 Android 配置段
> android配置已废弃，一般不需要进行测试和兼容

---

## 4. 源代码架构（分层 MVC）

```
src/
├── main.cpp                 # 入口：创建 Application，启动 MainWindow
├── app/                     # 应用层 — 组装各子系统，主窗口 UI 编排
│   ├── Application          # QApplication 子类，持有全局控制器指针（依赖注入根）
│   ├── MainWindow           # 主窗口（~3700行），UI 布局 + 菜单/工具栏/快捷键
│   ├── MainWindowPrivate    # PIMPL 私有数据类
│   ├── MainWindowDialogs    # 对话框相关逻辑（打开谱面等）
│   └── MainWindowSkinAudio  # 皮肤/音频菜单逻辑
├── controller/              # 控制器层 — 业务逻辑，实现 Undo/Redo
│   ├── ChartController      # 谱面编辑核心控制器，Command 模式撤销栈
│   ├── PlaybackController   # 播放状态控制（播放/暂停/定位）
│   └── SelectionController  # 选曲与音符选中管理
├── model/                   # 模型层 — 纯数据结构
│   ├── Note                 # 单个音符（Normal/Sound/Rain）
│   ├── BpmEntry             # BPM 变化点
│   ├── Chart                # 谱面容器（Notes + BPMs + MetaData）
│   ├── MetaData             # 谱面元数据（标题、作者、音频文件等）
│   └── Skin                 # 皮肤配置（颜色、背景等）
├── ui/                      # 视图层 — Widget 组件
│   ├── CustomWidgets/
│   │   ├── ChartCanvas/     # 核心画布（音符渲染+交互）
│   │   ├── RightPanel       # 右侧面板基类
│   │   └── RealtimePreviewWidget # 实时预览控件
│   ├── LeftPanel            # 左侧面板：播放/缩放/插件按钮
│   ├── NoteEditPanel        # 音符编辑面板（模式选择、网格、镜像等）
│   ├── BPMTimePanel         # BPM 编辑面板
│   ├── MetaEditPanel        # 元数据编辑面板
│   ├── TimelineWidget       # 底部时间轴
│   ├── DensityCurve         # 密度曲线/拖拽定位条
│   ├── SpeedPopup           # 速度选择弹出菜单
│   └── dialogs/             # 子对话框（PluginManagerDialog 等）
├── render/                  # 渲染层 — 画布绘制逻辑
│   ├── NoteRenderer         # 音符绘制
│   ├── GridRenderer         # 网格线绘制
│   ├── BackgroundRenderer   # 背景图加载与缓存
│   └── BeatDivisionColor    # 节拍分母→颜色映射
├── plugin/                  # 插件框架
│   ├── PluginInterface      # 插件基类接口
│   ├── PluginManager        # 插件生命周期管理（加载/验证/初始化）
│   ├── PluginLoader         # 动态库加载器
│   └── ExternalProcessPlugin # 外部进程插件适配器（stdin/stdout JSON 行协议）
├── file/                    # 文件 I/O 层
│   ├── ChartIO              # .mc 谱面 JSON 读写
│   ├── ProjectIO            # .mcz 打包文件读写
│   ├── ChartFileSystem      # 谱面文件系统注册表
│   └── PluginLoader         # 原生 DLL 插件动态加载
├── audio/                   # 音频子系统
│   ├── AudioPlayer          # QMediaPlayer 封装
│   ├── BpmDetector          # BPM 自动检测
│   ├── NoteSoundPlayer      # 音符音效播放
│   └── autotiming/          # 自动拍点检测（DSP/FFT 自研算法）
└── utils/                   # 工具函数
    ├── MathUtils            # 数学工具（beat↔ms转换、网格吸附等）
    ├── Settings             # 应用设置持久化（QSettings）
    └── Logger               # 日志系统
```

### 依赖关系（从上到下）
```
app/ ──→ controller/ ──→ model/
  │           │
  └──→ ui/ ──┘
  │     │
  │     └──→ render/
  │
  ├──→ plugin/
  ├──→ file/
  ├──→ audio/
  └──→ utils/
```

> `model/` 是纯数据结构，不依赖其他任何模块。`controller/` 操作 model，app 组装一切。

---

## 5. 核心数据模型

### Note (`src/model/Note.h`)
三种类型（`NoteType` 枚举）：
- **NORMAL (0)**：普通抓捕音符，需指定 `x` 位置（0–512）
- **SOUND (1)**：音效音符，需 `sound`/`vol`/`offset` 字段
- **RAIN (3)**：长按音符，需 `x` 和 `end_beat`（结束位置）

每个 Note 都有 `beatNum`/`numerator`/`denominator` 表示**有理数节拍位置**，以及 UUID 唯一标识。

### BpmEntry (`src/model/BpmEntry.h`)
BPM 变化点：`(beatNum, numerator, denominator, bpm)`。

### Chart (`src/model/Chart.h`)
谱面聚合容器：
- `QVector<Note> notes`
- `QVector<BpmEntry> bpms`
- `MetaData meta`
- 提供 `sortNotes()`, `isValid()`, `clear()` 等方法

### MetaData (`src/model/MetaData.h`)
```cpp
struct MetaData {
    QString title, titleOrg, artist, artistOrg;
    int difficulty = 0;
    QString chartAuthor;
    QString audioFile, backgroundFile;
    double previewTime = -1.0;
    double firstBpm = 120.0;
    double offset = 0.0;
    double speed = 1.0;
};
```

---

## 6. 控制器层

### ChartController (`src/controller/ChartController.h`)
谱面编辑的核心控制器，使用 **Command 模式**实现撤销/重做：
- `batchEdit()` — 批量编辑（返回 Command 对象）
- `moveNotes()` — 移动音符
- `addNote()` / `removeNote()` — 增删音符
- BPM CRUD 操作
- 完整的 `undo()` / `redo()` 栈
- 支持 `beginMacro`/`endMacro` 宏操作（多个编辑合并为一个撤销单元）

### PlaybackController (`src/controller/PlaybackController.h`)
播放控制：
- `play()` / `pause()` / `stop()`
- `setPosition()` — 跳转到指定时间
- 信号：`positionChanged()` 用于同步画布播放位置

### SelectionController (`src/controller/SelectionController.h`)
管理选中状态：当前选中的音符集合、谱面文件选择。

---

## 7. UI 布局

### MainWindow 布局结构（参见 `src/app/MainWindow.cpp`）
```
┌──────────┬───────────────────────┬──────────┐
│ LeftPanel│    ChartCanvas        │RightPanel│
│ 播放控制 │    (核心绘图区)       │ BPM编辑  │
│ 缩放按钮 │                       │ 音符编辑 │
│ 插件按钮 │                       │ 元数据   │
│          │                       │ 编辑     │
├──────────┴───────────────────────┴──────────┤
│              TimelineWidget                  │
└──────────────────────────────────────────────┘
```

### ChartCanvas (`src/ui/CustomWidgets/ChartCanvas/`)
核心画布组件，负责：
- 音符渲染（委托给 `NoteRenderer`）
- 鼠标交互（点击选中、拖拽移动、右键菜单）
- 键盘导航
- 与 `PlaybackController` 同步播放线

### 右侧面板（`src/ui/CustomWidgets/RightPanel`）
三个子面板通过 QStackedWidget 切换：
1. `BPMTimePanel` — BPM 编辑
2. `NoteEditPanel` — 音符编辑工具
3. `MetaEditPanel` — 元数据编辑

---

## 8. 渲染系统 (`src/render/`)

| 渲染器 | 职责 |
|--------|------|
| `NoteRenderer` | 绘制所有音符（Normal/Sound/Rain 三种形状） |
| `GridRenderer` | 绘制背景网格线和节拍线 |
| `BackgroundRenderer` | 加载背景图、调节亮度、生成缓存缩略图 |
| `BeatDivisionColor` | 按节拍分母返回不同颜色（1/2、1/3、1/4 等） |

渲染器是无状态的，由 `ChartCanvas` 在 `paintEvent()` 中按顺序调用。渲染器接收 `QPainter*` 和配置参数进行绘制。

---

## 9. 插件系统 (`src/plugin/` + `plugins/`)

### 两种插件类型

| 类型 | 实现方式 | 清单文件 | 通信协议 |
|------|---------|---------|---------|
| **Native** | C++ 动态库 (.dll/.so/.dylib) | 无（直接导出 C 函数） | 函数调用 |
| **Process** | 任意语言独立进程 | `*.plugin.json` | stdin/stdout JSON 行协议 |

### Native 插件导出的 C 函数
```c
PluginInterface* createPlugin();
void destroyPlugin(PluginInterface*);
int pluginApiVersion();  // 当前宿主版本 = 3
```

### Process 插件清单 (`*.plugin.json`)
JSON 文件包含：`pluginId`, `displayName`, `version`, `apiVersion`, `executable`, `args`, `workingDirectory`

### 核心接口：`PluginInterface` (`src/plugin/PluginInterface.h`)
插件需实现的关键方法：
- `pluginInfo()` — 返回 `PluginInfo` 元数据
- `initialize(QWidget* parent)` — 初始化
- `uninitialize()` — 清理
- `onChartLoaded(Chart* chart)` — 谱面加载回调
- `onPlaybackStateChanged(...)` — 播放状态变化回调
- 返回 `QWidget*` 的 UI 面板（在右侧面板显示）

### 加载流程 (`PluginManager`)
```
1. scanDirectories()        — 扫描 plugins/ 目录
2. 冲突消解                  — 按 displayName 去重，builtin.* 优先
3. 验证阶段                  — 检查 pluginId 唯一性、API 版本兼容性
4. initializePending()      — QTimer::singleShot 延迟初始化（避免阻塞 UI）
5. 生命周期管理              — 加载/卸载/重载
```

### 内置插件 (`plugins/builtin/`)
- **note_chain_assist** — Note 串辅助工具，帮助编排连续音符序列。使用外部进程模式（Python），清单位于 `plugins/builtin/note_chain_assist/`
- **note_color_formatter** — 音符颜色格式化插件

### 示例插件 (`plugins/samples/`)
提供插件开发参考模板。

### 插件同步脚本
`plugins/sync_plugins_to_builds.ps1` — 将插件文件同步到构建输出目录

---

## 10. 文件格式

### .mc 文件（谱面 JSON）
由 `ChartIO` 读写，JSON 结构包含：
- `meta` — 元数据对象
- `notes` — 音符数组
- `bpms` — BPM 变化点数组

### .mcz 文件（打包格式）
由 `ProjectIO` + `ChartFileSystem` 处理：
- 本质是 ZIP 包，内含 `.mc` 谱面文件 + 音频/背景资源
- `ChartFileSystem` 维护谱面注册表 (`chart_registry.json`)

### 关键类
- `ChartIO` — 单谱面文件读写
- `ProjectIO` — 工程目录扫描、难度提取、打包解包
- `ChartFileSystem` — 谱面文件注册与发现

---

## 11. 文档有效性矩阵

| 文档 | 有效性 | 说明 |
|------|--------|------|
| `docs/help.md` | ✅ 有效 | 用户帮助手册（263行），涵盖菜单/面板/画布/插件/快捷键 |
| `docs/history.md` | ✅ 有效 | 更新日志（407行），Alpha v0.0.1 → Beta v1.10.2 |
| `docs/NOTE_CHAIN_ASSIST_PLUGIN_DESIGN.md` | ✅ 有效 | Note Chain Assist 插件设计细化稿 |
| `docs/AI_PROJECT_GUIDE.md` | ✅ 有效 | **本文档** |
| `docs/version.md` | ⚠️ 半有效 | 仅写版本号，兼容性说明为空白 |
| `docs/about.md` | ❌ 空壳 | 声明用途但无实际内容，可忽略 |
| `TESTING.md` | ✅ 有效 | 测试指南，位于项目根目录 |
| `plugins/README.md` | ✅ 有效 | 插件系统说明 |
| `readme.md` | ✅ 有效 | 项目根目录 README |

---

## 12. AI 协作注意事项

### 关键文件快速索引
| 查找目标 | 路径 |
|----------|------|
| 入口点 | `src/main.cpp` |
| 构建配置 | `CMakeLists.txt` |
| 主窗口 | `src/app/MainWindow.cpp` / `.h` |
| 音符模型 | `src/model/Note.h` |
| 谱面模型 | `src/model/Chart.h` |
| 元数据模型 | `src/model/MetaData.h` |
| 谱面控制器 | `src/controller/ChartController.cpp` / `.h` |
| 播放控制器 | `src/controller/PlaybackController.cpp` / `.h` |
| 画布 | `src/ui/CustomWidgets/ChartCanvas/` |
| 音符渲染 | `src/render/NoteRenderer.cpp` / `.h` |
| 谱面 I/O | `src/file/ChartIO.cpp` / `.h` |
| 工程 I/O | `src/file/ProjectIO.cpp` / `.h` |
| 插件接口 | `src/plugin/PluginInterface.h` |
| 插件管理器 | `src/plugin/PluginManager.cpp` / `.h` |
| 外部进程插件 | `src/plugin/ExternalProcessPlugin.cpp` / `.h` |
| 音频播放 | `src/audio/AudioPlayer.cpp` / `.h` |
| BPM 检测 | `src/audio/BpmDetector.cpp` / `.h` |
| 数学工具 | `src/utils/MathUtils.cpp` / `.h` |
| 应用设置 | `src/utils/Settings.cpp` / `.h` |
| 测试 | `tests/minimal_tests.cpp` |

### 常用搜索模式
- 找 UI 组件：`src/ui/` 目录
- 找渲染逻辑：`src/render/` 目录
- 找数据操作：`src/controller/` 目录
- 找数据定义：`src/model/` 目录
- 找 I/O 操作：`src/file/` 目录
- 找音频相关：`src/audio/` 目录
- 找插件相关：`src/plugin/` 目录

### 修改代码注意事项
1. **model/ 是纯数据结构** — 修改 model 时注意不要引入对 controller 或 ui 的依赖
2. **ChartController 使用 Command 模式** — 修改编辑操作时确保 Undo/Redo 正确性
3. **Note 的节拍位置是有理数分数** — 不要假设它是浮点数，需通过 `beatToMs()` 转换
4. **插件变更需要同步** — 修改 `PluginInterface` 时需检查 `BuiltinPlugin` 和 `ExternalProcessPlugin` 的适配
5. **测试文件** — `tests/minimal_tests.cpp` 包含 70 个测试用例，覆盖 MathUtils/Chart/ChartIO/ChartController 等核心模块

### 非代码资源
- **翻译文件**：`resources/translations/`
- **默认皮肤**：`resources/default_skin/`
- **音符音效**：`resources/note_sounds/`
- **示例谱面**：`beatmap/` （若存在）
- **辅助工具**：`tools/perf/`（性能分析工具）

---

## 附录：模块依赖关系图（Mermaid 思维导图风格）

```
Malody Catch Editor
├── 入口：main.cpp → Application
├── 核心 UI：MainWindow
│   ├── LeftPanel（播放/缩放/插件）
│   ├── ChartCanvas（核心画布）
│   │   ├── NoteRenderer
│   │   ├── GridRenderer
│   │   ├── BackgroundRenderer
│   │   └── BeatDivisionColor
│   ├── RightPanel
│   │   ├── BPMTimePanel
│   │   ├── NoteEditPanel
│   │   └── MetaEditPanel
│   └── TimelineWidget
├── 控制器
│   ├── ChartController（Undo/Redo）
│   ├── PlaybackController
│   └── SelectionController
├── 模型
│   ├── Note (Normal/Sound/Rain)
│   ├── BpmEntry
│   ├── Chart
│   ├── MetaData
│   └── Skin
├── 文件 I/O
│   ├── ChartIO (.mc)
│   ├── ProjectIO (.mcz)
│   └── ChartFileSystem
├── 插件框架
│   ├── PluginInterface
│   ├── PluginManager
│   ├── PluginLoader（原生 DLL）
│   └── ExternalProcessPlugin（进程插件）
├── 音频
│   ├── AudioPlayer (QMediaPlayer)
│   ├── BpmDetector
│   └── NoteSoundPlayer
└── 工具
    ├── MathUtils (beat↔ms)
    ├── Settings (QSettings)
    └── Logger