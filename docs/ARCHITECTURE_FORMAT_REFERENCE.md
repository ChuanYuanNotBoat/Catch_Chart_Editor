# Malody Catch Editor — 谱面架构 / 格式 / 插件 / 外置关联文件 参考文档

> **目的**：完整描述当前编辑器的数据模型、文件格式、插件体系、坐标系统、外置 sidecar 文件格式等，为重构格式与导出到标准 mcz/mc 提供精确的结构定义。
>
> **版本**：Beta v1.10.5 | **最后更新**：2026-08-09
>
> ⚠️ **核心约束**：`.mc` 文件的 JSON 结构**永远不能更改**，必须保持与 Malody 官方格式完全兼容。所有扩展数据必须存放在 `.mcce-plugin/` 或独立辅助文件中。如果需要更改，则需要保证有可以导出为规范.mc/mcz的能力。

---

## 目录

1. [数据模型层](#1-数据模型层)
2. [.mc 谱面文件格式](#2-mc-谱面文件格式)
3. [.mcz 打包格式](#3-mcz-打包格式)
4. [坐标系统与时间映射](#4-坐标系统与时间映射)
5. [皮肤系统](#5-皮肤系统)
6. [插件系统](#6-插件系统)
7. [外置 Sidecar 文件体系](#7-外置-sidecar-文件体系)
8. [NoteChain 曲线编辑子系统](#8-notechain-曲线编辑子系统)
9. [外部进程插件协议](#9-外部进程插件协议)
10. [ChartFileSystem 文件注册表](#10-chartfilesystem-文件注册表)
11. [关键文件路径速查](#11-关键文件路径速查)
12. [附录 A：Malody 引擎 MCZ 解析机制](#12-附录-amalody-引擎-mcz-解析机制)
13. [附录 B：osu! Catch the Beat 谱面解析](#13-附录-bosu-catch-the-beat-谱面解析)

---

## 1. 数据模型层

位置：`src/model/`

### 1.1 Note — 音符

```cpp
// src/model/Note.h
enum class NoteType : int {
    NORMAL = 0,  // 普通音符
    SOUND  = 1,  // 音效音符（音频绑定）
    RAIN   = 3   // Rain 长按音符
};

struct Note {
    // ——— 拍号（有理数）———
    // 实际拍数 = beatNum + numerator / denominator
    int beatNum;       // 整数拍（从 0 开始）
    int numerator;     // 分子
    int denominator;   // 分母（分母为 0 视为无效）

    QString id;        // UUID 唯一标识（⚠️ 仅编辑器内部使用，不序列化到 .mc）

    NoteType type;     // 音符类型枚举

    // ——— 横坐标 ———
    // Normal / Rain: x ∈ [0, 512]
    // Sound:         x = -1（不参与横向渲染）
    int x;

    // ——— Rain 专属 ———
    bool isRain;           // 冗余字段（= type == RAIN）
    int endBeatNum;        // 结束拍号（整数拍）
    int endNumerator;      // 结束分子
    int endDenominator;    // 结束分母

    // ——— Sound 专属 ———
    QString sound;  // 音效文件名（也是 Malody V 识别音频的锚点）
    int vol;        // 音量 [0, 100]
    int offset;     // 偏移量（毫秒）
};
```

**Note 类型判别规则**（在 `.mc` JSON 中）：

- `type` 字段缺失或 `type = 0` → NORMAL
- `type = 1` 且包含 `sound` 字段 → SOUND
- `type = 3` 且包含 `endbeat` 和 `x` 字段 → RAIN

**关键行为**：

- `id` 字段仅编辑器内部使用，**不被序列化到 .mc JSON**
- 同一拍号允许存在多个 NORMAL note（允许多押）
- SOUND note 的 `x` 字段不参与渲染/Catch 判定；在 `.mc` JSON 中不序列化 `x`
- Malody V **通过 `note[type=1].sound` 识别音频**，而非 `meta.audio`

### 1.2 BpmEntry — BPM 变化点

```cpp
// src/model/BpmEntry.h
struct BpmEntry {
    int beatNum;       // 整数拍
    int numerator;     // 分子
    int denominator;   // 分母
    double bpm;        // BPM 值（> 0，如 120.0）
};
```

**规则**：

- BPM 列表按拍号升序排列
- 每个 BPM 条目定义从该拍号开始到下一变化点的 BPM 值
- 必须至少有一个 BPM 条目；默认值为 `[0, 1, 1, 120.0]`
- `denominator` 为 0 或 `bpm <= 0` 的条目视为无效

### 1.3 MetaData — 谱面元数据

```cpp
// src/model/MetaData.h
struct MetaData {
    QString title;          // 歌曲标题
    QString titleOrg;       // 原始语言标题
    QString artist;         // 艺术家
    QString artistOrg;      // 原始语言艺术家
    QString difficulty;     // 难度名（对应 .mc JSON 中的 "version"）
    QString chartAuthor;    // 谱面作者（对应 .mc JSON 中的 "creator"）
    QString audioFile;      // 音频文件名（仅文件名，非路径）
    QString backgroundFile; // 背景图片文件名（仅文件名，非路径）
    int previewTime;        // 预览起始时间（毫秒）
    double firstBpm;        // 第一个 BPM
    int offset;             // 全局偏移（毫秒）
    int speed;              // 下落速度（mode_ext.speed）
};
```

### 1.4 Chart — 谱面容器

```cpp
// src/model/Chart.h
class Chart {
    QVector<Note> m_notes;         // 音符列表（按拍号排序）
    QVector<BpmEntry> m_bpmList;   // BPM 列表（按拍号排序）
    MetaData m_meta;               // 元数据
};
```

**排序规则**：

1. 按 `beatNum` 升序
2. `beatNum` 相同时按分子/分母的浮点值升序
3. 同一拍号下：非 SOUND note 优先于 SOUND note
4. 同一拍号 + 同一类型下：按 `x` 升序

---

## 2. .mc 谱面文件格式

`.mc` 文件是 **UTF-8 编码的 JSON 文件**，与 Malody 官方格式 100% 兼容。

### 2.1 完整 JSON 结构（保存时实际输出）

```json
{
    "meta": {
        "$ver": 0,
        "creator": "谱面作者",
        "background": "bg.jpg",
        "version": "难度名",
        "id": 0,
        "mode": 3,
        "time": 1692123456,
        "song": {
            "title": "歌曲名",
            "artist": "艺术家",
            "id": 0
        },
        "mode_ext": {
            "speed": 1
        }
    },
    "time": [
        { "beat": [0, 0, 1], "bpm": 120.0 }
    ],
    "note": [
        { "beat": [0, 0, 1], "x": 256 },
        { "beat": [2, 0, 1], "type": 3, "x": 256, "endbeat": [3, 0, 1] },
        { "beat": [0, 0, 1], "type": 1, "sound": "audio.ogg", "vol": 100, "offset": 0 }
    ],
    "extra": {
        "test": {
            "divide": 4,
            "speed": 100,
            "save": 0,
            "lock": 0,
            "edit_mode": 0
        }
    }
}
```

**条件性出现的 meta 字段**（仅在值非空/非零时写入）：

| 字段 | 条件 | 内容 |
|------|------|------|
| `meta.song.titleorg` | titleOrg 非空 | 原语言标题 |
| `meta.song.artistorg` | artistOrg 非空 | 原语言艺术家 |
| `meta.audio` | audioFile 非空 | 音频文件名 |
| `meta.preview` | previewTime ≠ 0 | 预览时间 (ms) |
| `meta.offset` | offset ≠ 0 | 全局偏移 (ms) |
| `meta.bpm` | firstBpm > 0 | 第一 BPM 值 |

### 2.2 字段详细定义

#### `meta` 对象（保存时实际字段）

| JSON 键 | 类型 | 写入 | 说明 |
|----------|------|:---:|------|
| `meta.$ver` | Int | 始终 | 固定 0（版本标记） |
| `meta.creator` | String | 始终 | 谱面作者 |
| `meta.background` | String | 始终 | 背景文件名（仅文件名，可为空串） |
| `meta.version` | String | 始终 | **难度名** |
| `meta.id` | Int | 始终 | 固定 0（歌曲 ID） |
| `meta.mode` | Int | 始终 | 固定 3（Catch 模式标识） |
| `meta.time` | Int | 始终 | Unix 时间戳 |
| `meta.song` | Object | 始终 | 嵌套歌曲信息 |
| `meta.song.title` | String | 始终 | 歌曲标题 |
| `meta.song.artist` | String | 始终 | 艺术家 |
| `meta.song.id` | Int | 始终 | 固定 0 |
| `meta.song.titleorg` | String | 条件 | 原语言标题（titleOrg 非空） |
| `meta.song.artistorg` | String | 条件 | 原语言艺术家（artistOrg 非空） |
| `meta.mode_ext` | Object | 始终 | 模式扩展设置 |
| `meta.mode_ext.speed` | Int | 始终 | 下落速度 |
| `meta.audio` | String | 条件 | 音频文件名（audioFile 非空） |
| `meta.preview` | Int | 条件 | 预览时间 ms（previewTime != 0） |
| `meta.offset` | Int | 条件 | 全局偏移 ms（offset != 0） |
| `meta.bpm` | Float | 条件 | 第一 BPM（firstBpm > 0） |

**加载兼容性**：
- 歌曲优先 `meta.song.*`，回退平面字段 `meta.title` / `meta.artist` 等
- 速度：`meta.mode_ext.speed` > `meta.speed`
- 音频：`meta.audio` > note 中首个 type=1 的 sound 字段
- offset 还可从 note 中 type=1 的 offset 读取
#### `time` 数组 — BPM 表

| JSON 键         | 类型          | 必需 | 说明                     |
| --------------- | ------------- | ---- | ------------------------ |
| `time[].beat` | Array[Int×3] | ✅   | `[整数拍, 分子, 分母]` |
| `time[].bpm`  | Float         | ✅   | BPM 值，如`120.0`      |

**约束**：BPM 列表按拍号升序；`bpm > 0` 且 `denominator > 0`；至少 1 条（默认 `[0, 1, 1, 120.0]`）

#### `note` 数组 — 音符列表

**NORMAL 音符（type 缺失或 = 0）**：

| JSON 键         | 类型          | 必需 | 说明                     |
| --------------- | ------------- | ---- | ------------------------ |
| `note[].beat` | Array[Int×3] | ✅   | `[整数拍, 分子, 分母]` |
| `note[].x`    | Int           | ✅   | 横坐标 [0, 512]          |
| `note[].type` | Int           | 否   | 默认 0（NORMAL）         |

**SOUND 音符（type = 1）**：

| JSON 键           | 类型          | 必需 | 说明                     |
| ----------------- | ------------- | ---- | ------------------------ |
| `note[].beat`   | Array[Int×3] | ✅   | `[整数拍, 分子, 分母]` |
| `note[].type`   | Int           | ✅   | 必须为 1                 |
| `note[].sound`  | String        | ✅   | 音频文件名               |
| `note[].vol`    | Int           | 否   | 音量，默认 100           |
| `note[].offset` | Int           | 否   | 偏移毫秒，默认 0         |

**RAIN 音符（type = 3）**：

| JSON 键            | 类型          | 必需 | 说明            |
| ------------------ | ------------- | ---- | --------------- |
| `note[].beat`    | Array[Int×3] | ✅   | 起始拍号        |
| `note[].type`    | Int           | ✅   | 必须为 3        |
| `note[].x`       | Int           | ✅   | 横坐标 [0, 512] |
| `note[].endbeat` | Array[Int×3] | ✅   | 结束拍号        |

#### `extra` 对象 — 编辑器额外配置

| JSON 键                  | 类型 | 必需 | 说明               |
| ------------------------ | ---- | ---- | ------------------ |
| `extra.test.divide`    | Int  | 否   | 时间分度，默认 4   |
| `extra.test.speed`     | Int  | 否   | 播放速度，默认 100 |
| `extra.test.save`      | Int  | 否   | 自动保存，默认 0   |
| `extra.test.lock`      | Int  | 否   | 锁定，默认 0       |
| `extra.test.edit_mode` | Int  | 否   | 编辑模式，默认 0   |

### 2.3 保存时的自动修正

1. **音频 SOUND note 注入**：如果 note 数组中没有 `type=1` 但 `meta.audioFile` 非空，自动插入 `[0, 0, 1]` 处的 sound note
2. **资源路径清理**：`meta.background` / `meta.audio` / `sound` 只保存纯文件名
3. **extra 自动生成**：始终写入 `extra.test` 块
4. **拍号数组固定三元组**：`beat` 和 `endbeat` 始终为 `[整数拍, 分子, 分母]`

### 2.4 加载时的兼容补丁

1. **路径补丁**：若 meta 中 audio/background 包含完整路径，提取纯文件名并复制到 .mc 同目录
2. **长音频名重命名**：> 24 字符的音频文件名自动重命名为时间戳名
3. **资源冲突检测**：同目录同名但内容不同的资源，弹窗询问
4. **SHA256 校验**：加载 note 数组时记录 SHA256 哈希

### 2.5 Malody 引擎对 `.mc` 的解析（ParserMC）

以 Malody V 引擎源码 `ParserMC.cs` / `ParserMC.JsonDef.cs` 为基准，下列为 Malody 引擎完整识别的 `.mc` JSON 字段。编辑器**仅使用其中 Catch 模式需要的子集**，其余字段在保存时**不覆写也不删除**（round-trip safe）。

#### 引擎完整 JSON 模型

```
ParserMC.ChartData           // ← JSON root
├── meta: MetaData
│   ├── $ver: int             // JSON key "$ver"，内部为 mcVersion
│   ├── creator: string       // 谱面作者
│   ├── background: string    // 背景文件名
│   ├── cover: string         // [引擎独有] 封面图路径
│   ├── version: string       // ★ 难度名
│   ├── skin: string          // [引擎独有] 皮肤名（忽略）
│   ├── bga: string           // [引擎独有] BGA 文件名（忽略）
│   ├── video: string         // [引擎独有] 视频文件名（忽略）
│   ├── tags: string          // [引擎独有] 标签（忽略）
│   ├── free: int             // [引擎独有] 免费标记（忽略）
│   ├── preview: int          // 预览时间 ms
│   ├── mode: int             // 模式 0=Key/3=Catch/4=Pad/5=Taiko/6=Ring/7=Slide/8=Live/9=Cube
│   ├── aimode: string        // [引擎独有] AI 模式（忽略）
│   ├── song: MetaSongData
│   │   ├── id: int           // 歌曲 ID
│   │   ├── title: string     // 标题
│   │   ├── artist: string    // 艺术家
│   │   ├── titleorg: string  // 原语言标题
│   │   ├── artistorg: string // 原语言艺术家
│   │   ├── file: string      // [引擎独有] 音频文件名（旧字段，已废弃）
│   │   └── bpm: float        // [引擎独有] BPM（旧字段，已废弃）
│   └── mode_ext: MetaExtraData
│       ├── column: int       // [引擎独有] Key 模式列数（Catch 不使用）
│       ├── bar_begin: int    // [引擎独有] 4K SP bar_begin（Catch 不使用）
│       ├── speed: int        // 下落速度
│       ├── skin: string      // [引擎独有] mode_ext 皮肤（忽略）
│       └── bga: string       // [引擎独有] mode_ext BGA（忽略）
├── time[]: TimeData
│   ├── beat: Fraction        // [整数拍, 分子, 分母]
│   └── bpm: float            // BPM 值
├── effect[]: EffectData      // [引擎独有] 特效数组（编辑器不处理）
│   ├── beat: Fraction
│   ├── endbeat: Fraction
│   └── tuples: (...)
├── note[]: NoteData          // 音符数组（多模式共用）
│   ├── beat: Fraction
│   ├── endbeat: Fraction     // (Rain/长按)
│   ├── type: int             // 0=Normal, 1=Sound, 3=Rain, etc.
│   ├── x: short              // Catch/Slide 横坐标
│   ├── sound: string         // Sound note 音频
│   ├── vol: short            // 音量 (0-100)
│   ├── offset: int           // 偏移 ms
│   ├── index: ushort         // [引擎独有] Pad 起始位置
│   ├── endindex: ushort      // [引擎独有] Pad 结束位置
│   ├── w: short              // [引擎独有] Slide 宽度
│   ├── dir: int              // [引擎独有] Live 方向
│   ├── enddir: int           // [引擎独有] Live 结束方向
│   ├── seg: NoteSlideSegData[] // [引擎独有] Slide 段
│   └── group: ushort         // [引擎独有] 采样分组
├── noteref[]: NoteData       // [引擎独有] 引用音符（编辑器不处理）
└── extra: ExtraData
    ├── delays: Dict<string,int>    // [引擎独有] 采样延迟表
    └── custom: Dict<string,string> // [引擎独有] 自定义键值对
```

#### 编辑器 vs 引擎字段使用对比

| 类别 | 字段 | 编辑器使用 | 引擎使用 | 说明 |
|------|------|:---:|:---:|------|
| **meta** | `$ver` | ✅ | ✅ | 编辑器写入 0；引擎做版本兼容检查 |
| | `creator` | ✅ | ✅ | |
| | `background` | ✅ | ✅ | 仅文件名 |
| | `cover` | — | ✅ | 引擎额外识别，编辑器逐字保留 |
| | `version` | ✅ | ✅ | ★ 难度名 |
| | `skin` | — | ✅ | |
| | `bga` / `video` | — | ✅ | |
| | `tags` / `free` | — | ✅ | |
| | `preview` | ✅ (条件) | ✅ | |
| | `mode` | ✅ (写死 3) | ✅ | Catch = 3 |
| | `aimode` | — | ✅ | |
| **meta.song** | `id` / `title` / `artist` | ✅ | ✅ | |
| | `titleorg` / `artistorg` | ✅ (条件) | ✅ | |
| | `file` / `bpm` | — | ✅ | 旧字段，引擎兼容读取 |
| **meta.mode_ext** | `speed` | ✅ | ✅ | |
| | `column` / `bar_begin` | — | ✅ | Catch 模式不使用 |
| | `skin` / `bga` | — | ✅ | |
| **root** | `time[]` | ✅ | ✅ | BPM 变化点 |
| | `note[]` | ✅ | ✅ | 音符（仅 Normal/Sound/Rain） |
| | `effect[]` | — | ✅ | 特效（scroll speed / kiai 等） |
| | `noteref[]` | — | ✅ | 引用音符 |
| | `extra` | ✅ (test block) | ✅ | 编辑器只写 `extra.test` |
| | `extra.custom` | — | ✅ | 引擎自定义键值对 |
| | `extra.delays` | — | ✅ | 采样延迟 |

#### 解析流程（引擎）

```
ChartParser.CreateFromFilePath(filePath)
  ↓ 扩展名 .mc → dictExtFormat → FileFormat.MC
  ↓ new ParserMC()
  ↓ parser.Parse(metaOnly)
    ├── OnParseMeta(): 反序列化 JSON → ChartData
    │   ├── FillChartMeta(): 把 meta 映射到 MaChart.Meta
    │   │   ├── mode → ChartMode (MCKey/MCCatch/MCPad/...)
    │   │   ├── mode_ext.speed → UserSpeed
    │   │   └── 版本检查: mcVersion > Current → notSupportVersion
    │   └── meta.song.file / meta.audio → MainAudio
    └── OnParseDiff():
        ├── 遍历 note[] → CreateNote() → NoteType (Normal/Hold/Rain/Slide/...)
        ├── 处理 noteref[]（引用音符）
        ├── 处理 effect[]（特效指令）
        ├── 处理 extra.delays / extra.custom
        ├── UpdateOffsetFromBeat(): Beat → 毫秒转换
        └── 音频兼容: 唯一 sample 非 .ogg → 仅提示不阻断
```

> **关键兼容约定**：编辑器读写 `.mc` 时，引擎侧字段（`cover`/`skin`/`bga`/`video`/`tags`/`free`/`effect[]`/`noteref[]`/`extra.custom`/`extra.delays`）**逐字保留不修改**，确保 round-trip 后引擎读取不丢失数据。

## 3. .mcz 打包格式

`.mcz` 是 Malody 官方谱面分发格式，本质是 **ZIP 压缩包**。

### 3.1 打包内容

| 文件类型         | 扩展名                                                       | 必需 | 说明                             |
| ---------------- | ------------------------------------------------------------ | ---- | -------------------------------- |
| 谱面文件         | `.mc`                                                      | ✅   | 所有发现到的 .mc（含多难度）     |
| 音频文件         | `.ogg`, `.mp3`, `.wav`, `.flac`, `.m4a`, `.aac`  | 否   | .mc 中引用的                     |
| 背景图片         | `.jpg`, `.jpeg`, `.png`, `.bmp`, `.webp`, `.gif` | 否   | .mc 中引用的                     |
| 视频文件         | `.mp4`, `.mkv`, `.avi`, `.webm`, `.mov`            | 否   | 兼容保留                         |
| Sidecar BPM      | `.bpm_excludes.json`                                       | ✅   | 从`.mcce-plugin/` 打包         |
| Sidecar Song BPM | `.song_bpm.json`                                           | ✅   | 从`.mcce-plugin/` 打包         |
| Sidecar 曲线     | `.curve_tbd.json`                                          | 否   | 从`.mcce-plugin/` 打包（如有） |

### 3.2 打包规则

- `exportToMcz`：**目录全量打包**——把目录下所有文件（排除输出 .mcz 自身）全部打入 ZIP，不做类型过滤
- `exportToMczPure`：**白名单打包**——仅打包 `.mc` + 每个 .mc 引用的音频/背景/sound + 注册表中 `isRequired` sidecar，通过 `ChartFileSystem::isAllowedFile()` 过滤
- Sidecar 在 ZIP 中保持 `.mcce-plugin/{chartName}.{ext}` 的相对路径

### 3.3 内部 ZIP 结构

```
output.mcz
└── 0/
    ├── audio.ogg
    ├── bg.jpg
    ├── Easy.mc
    ├── Normal.mc
    └── .mcce-plugin/
        ├── Easy.song_bpm.json
        └── ...
```

所有文件统一放在 ZIP 根下的 `0/` 子目录内。
---

## 4. 坐标系统与时间映射

位置：`src/utils/MathUtils.h`

> ⚠️ **历史变更**：`CoordinateMapper` / `BeatLinearMapper` / `TimeLinearMapper` 及 `CoordinateMode` 切换已在 Beta v1.10.x 通过分支 `revert/remove-bpm-excludes` 回滚（见 `plans/实施计划_v2.md:130-133`）。当前只保留 **Beat-Linear 单一模式**，所有坐标转换直接通过 `MathUtils`。

### 4.1 坐标系（当前唯一：Beat-Linear）

Y 轴与拍号成线性比例，每拍等像素高度。这是默认模式，兼容所有旧版谱面。

```cpp
double beatToPixel(double beat, double scrollBeat, double visibleBeatRange, int height);
double pixelToBeat(int y, double scrollBeat, double visibleBeatRange, int height);
```

### 4.2 拍号 ↔ 毫秒转换

```cpp
// 核心公式：ms = accumulated + beatDelta * (60000.0 / bpm)

// 高性能缓存（O(log N) 二分查找）
struct BpmCacheEntry {
    double beatPos;        // 段起始拍号（浮点）
    double accumulatedMs;  // 累计毫秒（含 offset）
    double bpm;            // 该段 BPM
};
```

**转换流程**：

1. `buildBpmTimeCache()` — 一次构建，多次使用
2. `beatToMs()` — 二分查找目标拍号所在 BPM 段
3. `msToBeat()` — 逐段累计定位目标毫秒数对应的拍号
4. `offset` 通过减去 `offsetMs` 参与计算

### 4.3 吸附系统

```cpp
Note snapNoteToTime(Note, timeDivision);  // 时间分度吸附
int  snapXToGrid(x, gridDivision);        // X 轴整数网格吸附
int  snapXToBoundary(x);                  // X 边界吸附（±20px 阈值贴到 0 或 512）
```

---

## 5. 皮肤系统

位置：`src/model/Skin.h`, `src/file/SkinIO.h`

### 5.1 皮肤目录结构

```
skin_dir/
├── preview.json           # 皮肤元数据
├── catch-note-0.png       # 1/1 拍音符
├── catch-note-1.png       # 1/2 拍音符
├── catch-note-2.png       # 1/4 拍音符
├── catch-note-3.png       # 1/8/16/32 拍音符
├── catch-note-4.png       # 1/3/6/12/24 拍音符
├── catch-note-5.png       # Rain 音符
├── catch-bar.png          # 横栏
├── catch-light-0~16.png   # Light 效果
├── catch-light-arc.png    # Light 弧线（索引 17）
├── catch-light-bar.png    # Light 横栏（索引 18）
└── skin_config.json       # 校准配置（自动生成）
```

### 5.2 preview.json

```json
{ "title": "皮肤名称", "desc": "皮肤描述", "cover": "cover.png" }
```

### 5.3 skin_config.json

```json
{ "noteScales": [0.25, 0.25, 0.25, 0.25, 0.25, 0.25] }
```

`noteScales` — 6 个浮点数对应 noteType 0-5 的缩放因子，默认 0.25。

---

## 6. 插件系统

位置：`src/plugin/`, `src/file/PluginLoader.h`

### 6.1 插件类型

| 类型              | 载体                          | 加载方式                  | 适用场景                |
| ----------------- | ----------------------------- | ------------------------- | ----------------------- |
| **Native**  | `.dll/.so/.dylib`           | `QLibrary` 动态加载     | 高性能直操交互          |
| **Process** | `.plugin.json` + 可执行脚本 | `QProcess` stdin/stdout | 离线批处理、Python 工具 |

### 6.2 能力（Capability）清单

| 能力键 | 说明 |
|--------|------|
| `chart_observer` | 谱面变更通知 |
| `advanced_color_editor` | 高级颜色编辑 |
| `tool_actions` | 工具栏动作 + 侧栏/面板入口 |
| `floating_panel` | 浮动/停靠面板 |
| `canvas_overlay` | 画布叠加层渲染 |
| `host_batch_edit` | 批量编辑提交（单步 Undo） |
| `canvas_interaction` | 画布交互事件拦截 |
| `panel_workspace` | 多窗口停靠合并布局协作 |

### 6.3 PluginInterface 关键结构

```cpp
class PluginInterface {
    static constexpr int kHostApiVersion = 3;
    static constexpr int kMinSupportedPluginApiVersion = 2;

    // 元数据（必须实现）
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;
    virtual QString author() const = 0;
    virtual QString pluginSourcePath() const;
    virtual QString localizedDisplayName(const QString &locale) const;
    virtual QString localizedDescription(const QString &locale) const;
    virtual int pluginApiVersion() const = 0;
    virtual QStringList capabilities() const = 0;
    // 生命周期（必须实现）
    virtual bool initialize(QWidget *mainWindow) = 0;
    virtual void shutdown() = 0;
    // 可选生命周期事件
    virtual void onChartChanged();
    virtual void onChartLoaded(const QString &chartPath);
    virtual void onChartSaved(const QString &chartPath);
    virtual void onHostUndo(const QString &actionText);
    virtual void onHostRedo(const QString &actionText);
    virtual void onHostDiscardChanges(const QString &reasonText);
    // 可选 UI 扩展
    virtual bool openAdvancedColorEditor(const QVariantMap &context);
    // 可选能力接口
    virtual QList<ToolAction> toolActions() const;
    virtual bool runToolAction(const QString &actionId, const QVariantMap &context);
    virtual bool buildToolActionBatchEdit(actionId, context, BatchEdit *outEdit);
    virtual QList<FloatingPanelDescriptor> floatingPanels() const;
    virtual QWidget *createFloatingPanel(panelId, parent, context);
    virtual QList<CanvasOverlayItem> canvasOverlays(const QVariantMap &context) const;
    virtual bool handleCanvasInput(context, CanvasInputEvent, CanvasInputResult *out);
    // 可选工作区配置
    virtual QVariantMap panelWorkspaceConfig(const QVariantMap &context) const;
};

struct ToolAction {
    QString actionId, title, description;
    QString confirmMessage;   // 执行前确认提示（空则不弹窗）
    QString hostAction;       // ""（默认）| "undo" | "redo"
    QString placement;  // "tools_menu" | "top_toolbar" | "left_sidebar" |
                        // "right_note_panel" | "plugin_context_menu"
    bool requiresUndoSnapshot = true;
    bool checkable = false, checked = false;
    bool syncPluginToolModeWithChecked = false;
};

struct FloatingPanelDescriptor {
    QString panelId;
    QString title;
    QString description;
    QString panelRole;       // "primary" | "secondary" | "library"
    QString dockPreference;  // "left" | "right" | "bottom" | "float"
};

struct BatchEdit {
    QVector<Note> notesToAdd;
    QVector<Note> notesToRemove;
    QList<QPair<Note, Note>> notesToMove;  // (旧 → 新)
};
```

### 6.4 插件目录结构

```
{appDir}/plugins/
├── builtin/                  # 内置（优先加载）
│   ├── note_chain_assist/
│   │   ├── note_chain_assist.plugin.json
│   │   └── note_chain_assist.py
│   └── note_color_formatter/
│       ├── note_color_formatter.plugin.json
│       └── note_color_formatter.py
├── samples/                  # 示例（不会被加载）
└── *.dll / *.so / *.dylib   # Native 插件
```

### 6.5 插件冲突解决

- `builtin.*` 前缀的插件**优先**
- 同名插件：保持确定性顺序（先加载的优先）

---

## 7. 外置 Sidecar 文件体系

### 7.1 设计原则

1. **`.mc` 格式不可变**：所有扩展数据在 `.mcce-plugin/` 隐藏目录
2. **一对一关联**：sidecar 文件名 = `{chartStem}.{extension}`，与 .mc 文件名主干对应
3. **自动同步**：保存/复制/打开谱面时 sidecar 自动跟随
4. **注册表驱动**：通过 `ChartFileSystem` 统一管理

### 7.2 目录布局

```
歌曲目录/
├── song.ogg                        # 音频
├── bg.jpg                          # 背景
├── Easy.mc                         # 谱面
├── Normal.mc                       # 谱面
├── Hard.mc                         # 谱面
└── .mcce-plugin/                   # 隐藏 sidecar 目录
    ├── Easy.curve_tbd.json         # Easy 曲线工程
    ├── Normal.curve_tbd.json
    ├── Hard.curve_tbd.json
    ├── Easy.bpm_excludes.json      # BPM 排除项
    ├── Normal.bpm_excludes.json
    ├── Hard.bpm_excludes.json
    ├── Easy.song_bpm.json          # 歌曲 BPM
    ├── Normal.song_bpm.json
    └── Hard.song_bpm.json
```

**关联键**：`chartIdentifierForPath()` → 当前基于文件名主干，未来升级为 UUID。

### 7.3 已注册类型

| 扩展名（完整） | 描述 | 必需打包 | 优先级 |
|----------------|------|:-----:|:-----:|
| `mc` | Malody Chart File | 否 | 100 |
| `ogg`,`mp3`,`wav`,`flac`,`m4a`,`aac` | Audio | 否 | 90 |
| `jpg`,`jpeg`,`png`,`bmp`,`webp`,`gif` | Image | 否 | 90 |
| `mp4`,`mkv`,`avi`,`webm`,`mov` | Video | 否 | 90 |
| `curve_tbd.json` | Curve Sidecar | 否 | 80 |
| `bpm_excludes.json` | BPM Excludes | ✅ 是 | 80 |
| `song_bpm.json` | Song BPM | ✅ 是 | 80 |

### 7.4 Sidecar 生命周期

| 时机 | 行为 |
|------|------|
| 保存谱面 | `.mcce-plugin/` 中 sidecar 自动跟随 |
| 谱面复制 | 源 sidecar → 目标 sidecar 完整复制 |
| 难度切换 | 各难度 sidecar 独立隔离 |
| 导出 MCZ | 根据 `isRequired` + 是否存在决定是否打包 |
| 工作副本 | sidecar 在源谱面与工作副本间双向同步 |


## 8. NoteChain 曲线编辑子系统

位置：`src/editor/NoteChain/`

### 8.1 核心数据结构

```cpp
namespace NoteChain {

struct Anchor {
    int id = -1;              // 唯一 ID
    double laneX = 0.0;       // chart lane 坐标（0-512）
    double beat = 0.0;        // chart beat 坐标
    double handleInDx = 0.0;  // 入控制柄 lane 偏移（相对锚点）
    double handleInDy = 0.0;  // 入控制柄 beat 偏移
    double handleOutDx = 0.0; // 出控制柄 lane 偏移（相对锚点）
    double handleOutDy = 0.0; // 出控制柄 beat 偏移
};

struct Link { int fromAnchorId, toAnchorId; };
using LinkKey = QPair<int, int>;  // (minId, maxId)

constexpr const char *kShapeCurve    = "curve";
constexpr const char *kShapePolyline = "polyline";

struct CurveProjectMeta {
    QString filename;
    int revision = 0;         // CAS 版本号
    int anchorIdCounter = 0;
};
} // namespace NoteChain
```

### 8.2 State 管理（NoteChainState）

```cpp
class NoteChainState {
    QMap<int, Anchor> m_anchors;
    QSet<LinkKey> m_links;
    SegmentDenominatorMap m_segmentDenominators;  // LinkKey → int
    SegmentShapeMap m_segmentShapes;              // LinkKey → QString
    QSet<int> m_selectedAnchorIds;
    QSet<int> m_compoundSelection;               // 框选
    int m_nextAnchorId = 0;
    CurveProjectMeta m_projectMeta;
};
```

### 8.3 V3 Sidecar 格式（`.curve_tbd.json`）

```json
{
    "version": 3,
    "meta": { "revision": 0, "anchorIdCounter": 5 },
    "anchors": [
        {
            "id": 0,
            "lane_x": 256.0,
            "beat": 0.0,
            "handle_in_dx": 0.0,
            "handle_in_dy": 0.0,
            "handle_out_dx": 0.0,
            "handle_out_dy": 0.0
        }
    ],
    "links": [{ "from": 0, "to": 1 }],
    "segment_denominators": { "0_1": 4 },
    "segment_shapes": { "0_1": "curve" }
}
```

| 字段                              | 说明                                             |
| --------------------------------- | ------------------------------------------------ |
| `version`                       | 固定为 3                                         |
| `meta.revision`                 | CAS 版本号，每次保存 +1                          |
| `anchors[].lane_x`              | lane 坐标（chart 空间，0-512）                   |
| `anchors[].beat`                | beat 坐标（chart 空间）                          |
| `anchors[].handle_in/out_dx/dy` | 控制柄偏移                                       |
| `segment_denominators`          | 键`"from_to"` → 分母值                        |
| `segment_shapes`                | 键`"from_to"` → `"curve"` 或 `"polyline"` |

### 8.4 坐标体系

- Sidecar 中锚点使用 **Chart 坐标**（laneX + beat）
- 编辑器绘制时通过 `ChartCanvas` 转换到 Canvas 像素
- 提交为 note 时必须将采样点转为 `Note` 的 beat + x

### 8.5 文件路径推导

```cpp
// chart.mc → .mcce-plugin/chart.curve_tbd.json
static QString sidecarPathForChart(const QString &chartFilePath);
```

---

## 9. 外部进程插件协议

位置：`src/plugin/ExternalProcessPlugin.h/.cpp`

### 9.1 Manifest 格式（`.plugin.json`）

```json
{
    "pluginId": "tool.note_chain_assist",
    "displayName": "Note Chain Assist",
    "version": "1.0.0",
    "description": "Curve-driven note chain tool",
    "author": "Author",
    "pluginApiVersion": 3,
    "executable": "python",
    "args": ["note_chain_assist.py"],
    "capabilities": [
        "tool_actions",
        "canvas_overlay",
        "host_batch_edit",
        "canvas_interaction"
    ],
    "localizedDisplayName": {
        "zh_CN": "音符串辅助",
        "ja_JP": "ノートチェーン補助"
    },
    "localizedDescription": {
        "zh_CN": "曲线驱动音符串工具"
    }
}
```

### 9.2 线路协议

**底层通信**：`stdin/stdout` JSON-Lines（每行一个完整 JSON）

**Host → Plugin（事件通知）**：

```
{"event":"initialize","payload":{"plugin_id":"...","locale":"zh_CN","host_api_version":3}}
{"event":"chart_changed"}
{"event":"chart_loaded","payload":{"chart_path":"...","notes":[...],"bpm":[...]}}
{"event":"chart_saved","payload":{"chart_path":"..."}}
{"event":"host_undo","payload":{"action_text":"Add Note"}}
{"event":"host_redo","payload":{"action_text":"Add Note"}}
{"event":"host_discard_changes","payload":{"reason":"file_closed"}}
```

**Host → Plugin（方法调用）**：

```
{"method":"listToolActions","id":1,"payload":{}}
{"method":"runToolAction","id":2,"payload":{"action_id":"...","notes":[...],"locale":"zh_CN"}}
{"method":"buildBatchEdit","id":3,"payload":{"action_id":"...","notes":[...]}}
{"method":"listCanvasOverlays","id":4,"payload":{"notes":[...],"bpm":[...],"meta":{...}}}
{"method":"handleCanvasInput","id":5,"payload":{...}}
{"method":"openAdvancedColorEditor","id":6,"payload":{...}}
{"method":"getPanelWorkspaceConfig","id":7,"payload":{...}}
```

**Plugin → Host（回包）**：

```
{"id":1,"ok":true,"result":{"actions":[...]}}
{"id":2,"ok":false,"error":"Invalid request"}
```

### 9.3 超时设置

| 方法                        | 超时(ms) | 说明     |
| --------------------------- | :------: | -------- |
| `listToolActions`         |   500   | 轻量查询 |
| `listCanvasOverlays`      |    80    | 高频渲染 |
| `handleCanvasInput`       |   150   | 交互响应 |
| `buildBatchEdit`          |   8000   | 批量生成 |
| `runToolAction`           |  15000  | 工具执行 |
| `openAdvancedColorEditor` |  10000  | 复杂 UI  |
| `getPanelWorkspaceConfig` |   3000   | 配置查询 |

### 9.4 CanvasInputEvent

```cpp
struct CanvasInputEvent {
    QString type;  // mouse_down | mouse_move | mouse_up | wheel | key_down | key_up | focus_in | focus_out | cancel
    double x, y;   // Canvas 像素坐标
    int button, buttons, modifiers;
    bool shiftDown, ctrlDown;
    double wheelDelta;
    int key;
    qint64 timestampMs;
};

struct CanvasInputResult {
    bool consumed = false;
    QList<CanvasOverlayItem> overlay;
    BatchEdit previewEdit;
    QString cursor;  // CSS cursor
    QString statusText;
    bool requestUndoCheckpoint;
    QString undoCheckpointLabel;
};
```

### 9.5 CanvasOverlayItem

```cpp
struct CanvasOverlayItem {
    enum Kind { Line, Rect, Text };
    Kind kind;
    QPointF from, to;        // Line 端点（canvas 像素）
    QRectF rect;             // Rect 区域
    QString text;
    QColor color, fillColor;
    double width;
    int fontPx;
    bool chartSpace;              // 使用 chart 坐标（lane_x, beat）
    QPointF chartFrom;            // chart-space Line 起点（x=lane_x, y=beat）
    QPointF chartTo;              // chart-space Line 终点
    bool rectCenterOnChartPoint;  // Rect 以 chart 坐标点为中心
    bool noteSnapReference;       // 拖拽吸附参考线
};
```

---

## 10. ChartFileSystem 文件注册表

位置：`src/file/ChartFileSystem.h/.cpp`

```cpp
namespace ChartFileSystem {

struct RegisteredTypeInfo {
    QString extension;    // 扩展名（不含点），如 "bpm_excludes.json"
    QString description;
    bool isRequired;      // MCZ 打包时强制包含
    int priority;         // 数值越大越优先
};

class ChartFileSystemRegistry {
    static bool registerFileType(extension, description, isRequired, validator, priority);
    static bool unregisterFileType(extension);
    static bool isAllowedFile(relativePath);
    static QStringList requiredSidecarExtensions();
    static QVector<RegisteredTypeInfo> registeredFileTypes();
    static QString chartIdentifierForPath(chartPath);  // 基于文件名主干
};
}
```

**匹配规则**：`isAllowedFile(path)` → 提取后缀 → 注册表查找（大小写不敏感）→ 可选 validator 验证。支持复合扩展名（`curve_tbd.json` → `endsWith(".curve_tbd.json")`）。

---

## 11. 关键文件路径速查

### 11.1 源码结构（格式相关）

```

src/
├── model/
│   ├── Note.h / Note.cpp                      # 音符模型 (Normal/Sound/Rain)
│   ├── BpmEntry.h / BpmEntry.cpp              # BPM 变化点
│   ├── MetaData.h / MetaData.cpp              # 元数据
│   ├── Chart.h / Chart.cpp                    # 谱面容器
│   └── Skin.h / Skin.cpp                      # 皮肤
├── file/
│   ├── ChartIO.h / ChartIO.cpp                # .mc 读写（JSON 序列化）
│   ├── ProjectIO.h / ProjectIO.cpp            # .mcz 打包/解包
│   ├── ChartFileSystem.h / .cpp               # 文件类型注册表
│   ├── PluginLoader.h / PluginLoader.cpp      # 插件加载器
│   └── SkinIO.h / SkinIO.cpp                  # 皮肤 I/O
├── plugin/
│   ├── PluginInterface.h                      # 插件抽象接口
│   ├── PluginManager.h / PluginManager.cpp    # 插件管理器
│   └── ExternalProcessPlugin.h / .cpp         # 外部进程插件
├── editor/NoteChain/
│   ├── NoteChainCommon.h                      # 常量/类型
│   ├── NoteChainData.h                        # Anchor/Link/CurveProjectMeta
│   ├── NoteChainState.h / .cpp                # 运行时状态
│   └── NoteChainPersistence.h / .cpp          # V3 JSON 序列化
├── controller/
│   └── ChartController.h / .cpp               # 编辑控制器 + Undo/Redo
├── render/
│   ├── NoteRenderer.h                          # 音符渲染
│   ├── GridRenderer.h                          # 网格渲染
│   ├── BackgroundRenderer.h                    # 背景渲染
│   └── BeatDivisionColor.h                     # 分度颜色
└── utils/
    └── MathUtils.h / MathUtils.cpp             # beat↔ms 转换 / 吸附 / 像素映射

```

### 11.1b Malody 引擎源码（格式相关）

```
malody/Assets/Malody/Scripts/
├── Helper/
│   ├── FileUtil.cs               # IsPackFile / IsChartFile / IsIgnorePath
│   └── ZipUtil.cs                # UnzipFileWithName / ZipFolderNative
├── Framework/Chart/
│   ├── ChartUtil.cs              # DetectChartFormat / MCMode 转换
│   ├── Manager/
│   │   ├── ChartManager.cs       # ImportPack / ExportSong / Scan
│   │   ├── Song.cs               # ScanFiles / RebuildShowList
│   │   ├── Song.File.cs          # CollectChartFile / BuildChartsFromFile
│   │   └── Song.Meta.cs          # 元数据 DB 读写
│   ├── ChartData/
│   │   └── Chart.Define.cs       # FileFormat / FileMode / MCMode 枚举
│   └── Parser/
│       ├── ChartParser.cs         # 抽象解析器基类
│       ├── ChartParser.Factory.cs # CreateFromFilePath / dictExtFormat
│       ├── ParserMC.cs            # .mc 解析实现
│       ├── ParserMC.JsonDef.cs    # .mc JSON class 定义
│       └── ParserMC.JsonConverter.cs # Effect JSON 转换器
└── Utils/
    └── BundleChart.cs             # 内置捆绑谱面识别
```

### 11.2 数据文件位置

| 文件/目录 | 位置 | 说明 |
|-----------|------|------|
| `.mc` 谱面 | 歌曲目录下 | 主谱面数据（JSON） |
| `.mcz` 谱面包 | 任意位置 | ZIP 打包分发 |
| `.mcce-plugin/` | 歌曲目录下 | 隐藏 sidecar 目录 |
| `curve_tbd.json` | `.mcce-plugin/{stem}.curve_tbd.json` | 曲线工程（V3） |
| `bpm_excludes.json` | `.mcce-plugin/{stem}.bpm_excludes.json` | BPM 排除项 |
| `song_bpm.json` | `.mcce-plugin/{stem}.song_bpm.json` | 歌曲 BPM 数据 |
| `*.plugin.json` | `{appDir}/plugins/` | 进程插件清单 |
| `*.dll/.so/.dylib` | `{appDir}/plugins/` | 原生插件库 |
| `preview.json` | `{skinDir}/preview.json` | 皮肤元数据 |
| `skin_config.json` | `{skinDir}/skin_config.json` | 皮肤校准配置 |

### 11.3 .mc JSON 结构速查（编者字段 + [引擎独有]）

```
root
├── meta                           // 元数据对象
│   ├── $ver: 0                    // 版本标记
│   ├── creator                    // 谱面作者
│   ├── background                 // 背景文件名
│   ├── cover                      // [引擎] 封面图
│   ├── version                    // ★ 难度名
│   ├── skin                       // [引擎] 皮肤名
│   ├── bga                        // [引擎] BGA 文件
│   ├── video                      // [引擎] 视频文件
│   ├── tags                       // [引擎] 标签
│   ├── free: 0                    // [引擎] 免费标记
│   ├── id: 0                      // 歌曲 ID
│   ├── mode: 3                    // Catch 模式
│   ├── time: (timestamp)          // Unix 时间戳
│   ├── aimode                     // [引擎] AI 模式
│   ├── song
│   │   ├── title                  // 标题
│   │   ├── artist                 // 艺术家
│   │   ├── id: 0
│   │   ├── titleorg               // [条件] 原语言标题
│   │   ├── artistorg              // [条件] 原语言艺术家
│   │   ├── file                   // [引擎] 音频名（旧）
│   │   └── bpm                    // [引擎] BPM（旧）
│   └── mode_ext
│       ├── speed                  // 下落速度
│       ├── column                 // [引擎] Key 列数
│       ├── bar_begin              // [引擎] bar_begin
│       ├── skin                   // [引擎] 皮肤
│       └── bga                    // [引擎] BGA
├── time[]                         // BPM 数组
│   ├── beat: [beat, num, den]
│   └── bpm: 120.0
├── effect[]                       // [引擎] 特效数组
├── note[]                         // 音符数组
│   ├── Normal: {beat, x, type:0}
│   ├── Sound:  {beat, type:1, sound, vol, offset}
│   └── Rain:   {beat, type:3, x, endbeat}
├── noteref[]                      // [引擎] 引用音符
└── extra                          // 编辑器配置 + [引擎] 扩展
    ├── test
    │   ├── divide: 4
    │   ├── speed: 100
    │   └── save/lock/edit_mode: 0
    ├── delays                     // [引擎] 采样延迟表
    └── custom                     // [引擎] 自定义键值对

```

---

> **附录**：完整 JSON Schema 定义、跨格式映射表、版本兼容矩阵等，待后续补充。

---

## 12. 附录 A：Malody 引擎 MCZ 解析机制

> 依据源码：`malody/Assets/Malody/Scripts/Framework/Chart/`、`Helper/FileUtil.cs`、`Helper/ZipUtil.cs`

### 12.1 文件识别

`FileUtil.IsPackFile()` 通过扩展名判定打包文件：

```csharp
// FileUtil.cs:17-24
return ext is ".zip" or ".osz" or ".mcz" or ".mcb" or ".qp" or ".svc";
```

`.mcz` 在 Malody 引擎中被统一视作 **ZIP 压缩包**。谱面文件通过 `FileUtil.IsChartFile()` 识别（`.mc`、`.osu`、`.sm`、`.bms`、`.tja`、`.qua`、`.vsc` 等），非谱面文件（`.jpg`、`.png`、`.mp3`、`.ogg`、`.wav`、`.ini`）被显式排除。

### 12.2 导入流程（ImportPack）

```
用户选择 .mcz → ChartManager.ImportPack(path, keepZip)
  ├── FileUtil.IsPackFile() 验证
  ├── Path.GetExtension() 判断类型
  │   ├── ".mcb" → ZipUtil.UnzipFile(path, ChartInternal, true)  // 批量导入
  │   └── ".mcz" → ZipUtil.UnzipFileWithName(path, ChartInternal)
  │       └── 解压到 {ChartInternal}/{zipfilename}/
  │           └── SharpZipLib (ZipInputStream), UTF-8 编码
  ├── .mcb → ScanBundle()（遍历子目录每首曲目）
  └── .mcz → ReloadSong(newPath)（扫描单曲目录）
```

**关键源码**：`ChartManager.cs:295-330`

### 12.3 谱面扫描（Song.ScanFiles）

解压后，`Song.ScanFiles()` 递归扫描歌曲目录：

```
CollectChartFile(songPath, ref chartFiles)
  ├── 遍历子目录（跳过 __macosx / . 前缀）
  ├── 遍历文件
  │   ├── FileUtil.IsNotChartFile() → 跳过 (.jpg/.png/.mp3/.ogg/.wav/.ini)
  │   ├── FileUtil.IsChartFile() → 收集 (.mc/.osu/.sm/.bms/.tja/.qua/.vsc)
  │   └── ChartUtil.DetectChartFormat() → 尝试 JSON 格式识别
  └── 对每个谱面文件:
      ├── DB 中存在记录 → 从 DB 创建壳（跳过解析）
      └── DB 中不存在 → ChartParser 解析 + 存入 DB
```

**关键源码**：`Song.File.cs:24-114`

### 12.4 格式解析（ChartParser Factory）

```csharp
// ChartParser.Factory.cs:22-31
dictExtFormat = {
    [".mc"]  → FileFormat.MC   → ParserMC,
    [".osu"] → FileFormat.OSU  → ParserOSU,
    [".tja"] → FileFormat.TJA  → ParserTJA,
    [".bms"] → FileFormat.BMS  → ParserBMS,
    // ...
};
```

`.mc` 文件由 `ParserMC` 处理（详见 [2.5 节](#25-malody-引擎对-mc-的解析parsermc)），解析后填充 `MaChart` 对象（含 `timeList`、`noteList`、`effectList`、`sampleList`）。

### 12.5 多难度支持

同一歌曲目录下可以存在**多个 `.mc` 文件**，每个文件代表一个难度：

| 文件 | 对应难度 |
|------|----------|
| `song.mc` | Lv.1 (默认) |
| `song_hard.mc` | Lv.2 Hard |
| `song_expert.mc` | Lv.3 Expert |

引擎通过 `meta.version` 字段读取难度名。`MaChart.IsMultiDiffFormat` 判定是否为多难格式（如 OSU 文件内含多难），但 `.mc` 是单文件单难度。

### 12.6 Catch 模式标识

```
mode: 3  →  MCMode.Catch = 3        (Chart.Define.cs:44)
         →  FileMode.MCCatch = 0x80  (Chart.Define.cs:22)
```

以下是完整的 Malody 模式枚举：

| mode 值 | 模式 | FileMode |
|:-------:|------|----------|
| 0 | Key (下落式) | `MCKey = 0x40` |
| 3 | **Catch** | `MCCatch = 0x80` |
| 4 | Pad | `MCPad = 0x100` |
| 5 | Taiko | `MCTaiko = 0x200` |
| 6 | Ring | `MCRing = 0x800` |
| 7 | Slide | `MCSlide = 0x1000` |
| 8 | Live | `MCLive = 0x4000` |
| 9 | Cube | `MCCube = 0x8000` |

### 12.7 导出流程（ExportSong）

```csharp
// ChartManager.cs:332-346
ExportSong(Song song, string folder)
  ├── 创建目标文件夹
  ├── 安全文件名: ReplaceSafeFileName(title)
  └── ZipUtil.ZipFolderNative(song.Path, "{folder}/{title}.mcz")
      └── ICSharpCode.SharpZipLib, 压缩级别 5
      └── 打包 song 目录下所有文件（排除 ignore 路径）
```

引擎使用 C++ Native 方法 `_ZipFolderNative` 进行打包以优化性能。

### 12.8 MCB 捆绑包（.mcb）

`.mcb` 是 Malody 的批量谱面捆绑格式，与 `.mcz` 不同：

| 特性 | .mcz | .mcb |
|------|:----:|:----:|
| 内容 | 单个歌曲 | 多个歌曲 |
| 解压方式 | `UnzipFileWithName` | `UnzipFile(overrides=true)` |
| 扫描方式 | `ReloadSong(单路径)` | `ScanBundle(全目录)` |
| ZIP 结构 | `{name}/` 下直接是歌曲文件 | 根目录下每个子目录是一个歌曲 |

### 12.9 编辑器 vs 引擎差异总结

| 维度 | Catch Editor | Malody 引擎 |
|------|-------------|-------------|
| **ZIP 库** | PowerShell `Expand-Archive` (Win) / `unzip` (Unix) | `ICSharpCode.SharpZipLib` (C#) |
| **文件白名单** | `ChartFileSystem` 注册表（可动态注册） | 硬编码扩展名列表 |
| **Sidecar** | `.mcce-plugin/` 隐藏目录（V3 曲线数据等） | ❌ 不认识，会被打包进 .mcz |
| **DB 缓存** | 无（直接文件系统） | `UnqDB` 键值存储（song/chart meta） |
| **Multi-diff** | 单 .mc = 单难度 | 同目录多个 .mc = 多个难度 |
| **Effect** | 不支持 | 支持 `effect[]` 数组（ScrollSpeed 等） |
| **noteref** | 不支持 | 支持引用音符 |
| **打包** | PowerShell `ZipArchive` + `/` 分隔符 | Native `_ZipFolderNative` |
| **扩展名** | 仅 `.mcz` | `.mcz` / `.mcb` / `.osz` / `.qp` / `.svc` |

### 12.10 引擎关键源码路径

```
malody/Assets/Malody/Scripts/
├── Helper/
│   ├── FileUtil.cs               // IsPackFile / IsChartFile / IsIgnorePath
│   └── ZipUtil.cs                // UnzipFileWithName / ZipFolderNative
├── Framework/Chart/
│   ├── ChartUtil.cs              // DetectChartFormat / MCMode 转换
│   ├── Manager/
│   │   ├── ChartManager.cs       // ImportPack / ExportSong / Scan
│   │   ├── Song.cs               // ScanFiles / RebuildShowList
│   │   ├── Song.File.cs          // CollectChartFile / BuildChartsFromFile
│   │   └── Song.Meta.cs          // 元数据 DB 读写
│   ├── ChartData/
│   │   └── Chart.Define.cs       // FileFormat / FileMode / MCMode 枚举
│   └── Parser/
│       ├── ChartParser.cs         // 抽象解析器基类
│       ├── ChartParser.Factory.cs // CreateFromFilePath / dictExtFormat
│       ├── ParserMC.cs            // .mc 解析实现
│       ├── ParserMC.JsonDef.cs    // .mc JSON class 定义
│       └── ParserMC.JsonConverter.cs // Effect JSON 转换器
└── Utils/
    └── BundleChart.cs             // 内置捆绑谱面识别


---

## 13. 附录 B：osu! Catch the Beat 谱面解析

> 依据源码：`osu.Game.Rulesets.Catch/`、`osu.Game/Beatmaps/Formats/`、`osu.Game/Rulesets/Objects/Legacy/`

### 13.1 `.osu` 文件格式

`.osu` 是 INI 风格的分段文本文件，由 `LegacyDecoder` 解析（行式读取，跳过空白行和 `//` 注释）。

#### Section 枚举

```csharp
enum Section {
    General,        // 基本信息: AudioFilename, Mode, PreviewTime, etc.
    Editor,         // 编辑器书签/网格间距
    Metadata,       // 歌曲元数据: Title, Artist, Creator, Version, etc.
    Difficulty,     // CS, HP, OD, AR, SliderMultiplier, SliderTickRate
    Events,         // 背景/视频/break 时间
    TimingPoints,   // Timing (BPM) + Sample/Kiai 控制点
    Colours,        // 连击颜色 (Combo1~8)
    HitObjects,     // 物量（每行一个）
    CatchTheBeat,   // [CTB 独有] 手动定位偏移
    Mania,          // [Mania 独有] 键盘数
}
```

**CTB 判定**：`General` 中 `Mode: 2`。

#### TimingPoints 格式

```
time,beatLength,meter,sampleSet,sampleIndex,volume,uninherited,effects
```

- `uninherited=1`（红线）：重置 BPM，`beatLength` 为拍长（ms）
- `uninherited=0`（绿线）：继承 BPM，`beatLength` 为速度倍率（负百分比）

### 13.2 HitObject 行格式

```
x,y,time,type,hitSound,extras...
```

#### 字段

| 列 | 名称 | 说明 |
|:--:|------|------|
| 0 | `x` | X 坐标 (0~512) |
| 1 | `y` | Y 坐标 (0~384，CTB 不参与判定) |
| 2 | `time` | 起始时间（毫秒） |
| 3 | `type` | 类型 bitmask |
| 4 | `hitSound` | 打击音效 (0=Normal/2=Whistle/4=Finish/8=Clap) |
| 5+ | `extras` | 依赖 type 的额外字段 |

#### type bitmask（`LegacyHitObjectType`）

```
Circle    = 1       (0b00000001)
Slider    = 2       (0b00000010)
NewCombo  = 4       (0b00000100)
Spinner   = 8       (0b00001000)
ComboOffset = 7<<4  (0b01110000)
Hold      = 128     (0b10000000)
```

type=`Circle|NewCombo=5` → 新 combo 圆圈。`ComboOffset>>4` 得跳过数。

#### extras（按 type）

**Circle** (`type & 1`): `extras = sampleBank:addBank:customIndex:volume:filename`

**Slider** (`type & 2`):
```
curvePoints,repeatCount,length,nodeSounds,nodeAdditions,extras
```
- `curvePoints`: e.g. `"B|200:200|250:250|P|300:0"`
- `repeatCount`: ≥1 (stable 的重复数=实际段数+1)
- `length`: 像素长度
- `nodeSounds`: 每节点音效 `|` 分隔
- `nodeAdditions`: 每节点 sampleBank `|` 分隔

**Spinner** (`type & 8`): `extras = endTime`

#### Slider path 类型

| 字符 | 类型 | 说明 |
|:---:|------|------|
| `C` | Catmull | 默认曲线 |
| `B` | Bezier | `B` 后接度数（如 `B3`） |
| `L` | Linear | 直线段 |
| `P` | Perfect Curve | 完美圆弧（3 点） |

### 13.3 CTB 物量转换（CatchBeatmapConverter）

`CatchBeatmapConverter` 将 osu! 通用 `HitObject` 转换为 CTB 专用 `CatchHitObject`：

| osu! 输入 | CTB 输出 | 关键映射 |
|-----------|----------|----------|
| `IHasPathWithRepeats` (Slider) | `JuiceStream` | Path/RepeatCount/NodeSamples; TickDistanceMultiplier(v<8) |
| `IHasDuration` (Spinner) | `BananaShower` | Duration/Samples |
| 其他 (Circle) | `Fruit` | X=IHasXPosition.X; LegacyConvertedY=IHasYPosition.Y |

**常量**：`DEFAULT_LEGACY_CONVERT_Y = 192`

### 13.4 CTB 物量类型层级

```
CatchHitObject (abstract)
├── PalpableCatchHitObject (abstract)   ← 可被 catcher 接住
│   ├── Fruit               (circle → fruit)
│   ├── Droplet             (slider tick)
│   │   └── TinyDroplet     (slider 段内插值)
│   └── Banana              (BananaShower 中随机生成)
└── [容器]
    ├── JuiceStream          (slider 整体)
    └── BananaShower         (spinner 整体)
```

#### CatchHitObject 关键属性

| 属性 | 类型 | 说明 |
|------|------|------|
| `OriginalX` | float | beatmap 原始 X [0, 512] |
| `XOffset` | float | 随机偏移（Processor 计算） |
| `EffectiveX` | float | `Clamp(OriginalX+XOffset, 0, 512)` — 游戏实际 X |
| `LegacyConvertedY` | float | 保留原 Y（CTB 不使用） |
| `Scale` | float | 缩放（由 CircleSize 计算） |
| `IndexInBeatmap` | int | 物量序号 |
| `NewCombo` | bool | 是否新 combo |
| `ComboOffset` | int | combo 跳过数 |

#### JuiceStream 专有属性

| 属性 | 类型 | 说明 |
|------|------|------|
| `Path` | SliderPath | 路径控制点 |
| `RepeatCount` | int | 重复次数 |
| `SliderVelocityMultiplier` | double | sv 倍率 [0.1, 10] |
| `Velocity` | double | 像素/ms |
| `TickDistance` | double | `scoringDistance / SliderTickRate * TickDistanceMultiplier` |
| `EndX` | float | 末点 X |

#### PalpableCatchHitObject

| 属性 | 说明 |
|------|------|
| `HyperDash` | 是否超冲 |
| `HyperDashTarget` | 超冲目标物量 |
| `DistanceToHyperDash` | 距超冲阈值差值 |

### 13.5 坐标系统与尺寸

| 常量 | 值 | 来源 |
|------|:--:|------|
| `CatchPlayfield.WIDTH` | **512** | CatchPlayfield.cs |
| `CatchPlayfield.HEIGHT` | 384 | (CTB 不使用 Y) |
| `CatchPlayfield.CENTER_X` | 256 | — |
| `Catcher.BASE_SIZE` | **106.75** | Catcher 宽度 (1x) |
| `Catcher.ALLOWED_CATCH_RANGE` | 0.8 | 有效接住比例 |
| `Catcher.BASE_WALK_SPEED` | 0.5 | 步行 (px/ms) |
| `Catcher.BASE_DASH_SPEED` | 1.0 | 冲刺 (px/ms) |
| `CatchHitObject.OBJECT_RADIUS` | 64 | 水果半径 |

**与 Malody 对比**：Playfield 宽度同为 512，X ∈ [0, 512] 完全对齐。

### 13.6 HyperDash 机制

遍历可接物量（`Fruit` + `Droplet`，不含 `TinyDroplet`）：

```
halfCatcherWidth = CatcherWidth(difficulty) / 2 / ALLOWED_CATCH_RANGE

for i in 0..len-1:
    cur = palpable[i]; nxt = palpable[i+1]
    direction = nxt.EffectiveX > cur.EffectiveX ? 1 : -1
    timeToNext = (int)nxt.StartTime - (int)cur.StartTime - (1000/60/4)
    distanceToNext = |nxt.X - cur.X| - 余量
    distanceToHyper = timeToNext * BASE_DASH_SPEED - distanceToNext
    if distanceToHyper < 0:
        cur.HyperDashTarget = nxt   // 激活超冲
```

### 13.7 固定随机数与随机逻辑

#### RNG 种子

```csharp
// CatchBeatmapProcessor.cs:16
public const int RNG_SEED = 1337;
var rng = new LegacyRandom(RNG_SEED);  // 确定性伪随机
```

#### BananaShower — Banana 位置

```csharp
foreach (var banana in bananaShower.NestedHitObjects.OfType<Banana>()) {
    banana.XOffset = (float)(rng.NextDouble() * CatchPlayfield.WIDTH);  // [0, 512)
    rng.Next();  // osu!stable 消费: 香蕉类型
    rng.Next();  // osu!stable 消费: 香蕉旋转
    rng.Next();  // osu!stable 消费: 香蕉颜色
}
```

每个 banana 消费 **4 次** RNG。顺序：XOffset → 类型 → 旋转 → 颜色。

#### HardRock 偏移（`applyHardRockOffset`）

- 时间差 > 1000ms → 不偏移
- X 相同 → `rng.NextBool()` 方向 + `rng.Next(0, maxOffset)` 量 (上限 20)
- X 不同 → `|diff| < timeDiff/3` 时反向微调

> **关键**：固定 seed `1337` + 确定性 `LegacyRandom` → 同一谱面每次 Banana 分布和 HardRock 偏移完全相同。对导出验证至关重要。

### 13.8 导入/导出流程

#### 导入

```
用户选择 .osz → ZipUtil.UnzipFileWithName() → Song.ScanFiles()
  → CollectChartFile() 收集 .osu
  → ChartParser.CreateFromFilePath() → FileFormat.OSU → ParserOsu
```

`.osu` 解析链路：
```
LegacyBeatmapDecoder.ParseStreamInto()
  ├── 读取 "osu file format v{VER}" 头 (LATEST_VERSION=14)
  ├── 逐行 Section 分发:
  │   ├── General  → AudioFilename, Mode, PreviewTime
  │   ├── Metadata → Title, Artist, Creator, Version (难度名)
  │   ├── Difficulty → CS, HP, OD, AR
  │   ├── TimingPoints → TimingControlPoint, DifficultyControlPoint
  │   ├── HitObjects → ConvertHitObjectParser.Parse(line)
  │   │   ├── Circle → ConvertHitObject(circle)
  │   │   ├── Slider → convertPathString() + 节点采样
  │   │   └── Spinner → ConvertHitObject(spinner, duration)
  │   └── Colours → CustomComboColours
  ├── hitObjects.OrderBy(h => h.StartTime)
  └── applyDefaults + applySamples → Beatmap<HitObject>
```

#### 导出

```
ChartManager.ExportSong(song, folder)
  └── ZipUtil.ZipFolderNative(song.Path, "{folder}/{title}.zip")
```

### 13.9 关键源码路径

```
osu.Game.Rulesets.Catch/
├── Beatmaps/
│   ├── CatchBeatmap.cs              // GetPalpableObjects()
│   ├── CatchBeatmapConverter.cs     // osu!HitObject → CatchHitObject
│   └── CatchBeatmapProcessor.cs     // HyperDash / RNG(1337) / XOffset
├── Objects/
│   ├── CatchHitObject.cs            // 基类 (X/Y/Scale/Index)
│   ├── PalpableCatchHitObject.cs    // HyperDash/DistanceToHyperDash
│   ├── Fruit.cs / Droplet.cs / TinyDroplet.cs / Banana.cs
│   ├── JuiceStream.cs               // Path/RepeatCount/Velocity/TickDistance
│   └── BananaShower.cs              // Duration + Banana 生成
├── UI/
│   ├── CatchPlayfield.cs            // WIDTH=512, HEIGHT=384
│   ├── Catcher.cs                   // BASE_SIZE=106.75, ALLOWED_CATCH_RANGE=0.8
│   └── CatcherArea.cs

osu.Game/
├── Beatmaps/Formats/
│   ├── LegacyDecoder.cs             // Section 枚举 + 行解析
│   ├── LegacyBeatmapDecoder.cs      // Beatmap 解码 (LATEST_VERSION=14)
│   └── LegacyBeatmapEncoder.cs      // 编码回 .osu
├── Beatmaps/Legacy/
│   └── LegacyHitObjectType.cs       // Circle=1/Slider=2/NewCombo=4/Spinner=8
└── Rulesets/Objects/Legacy/
    └── ConvertHitObjectParser.cs    // HitObject 文本行解析 + path/curve
```
