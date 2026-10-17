# AutoTimingCore dependency notes

本文件记录 CCE 使用 AutoTimingCore 的来源边界、版本钉扎与更新流程。它是来源说明，不是许可证；不自行授予、限制或改变任何代码的使用权利。

## 版本钉扎

- 上游仓库：`https://github.com/ChuanYuanNotBoat/AutoTimingCore.git`
- 子模块路径：`third_party/AutoTimingCore`
- 固定提交：`23d3164b8a3fa2d8e9039a4fa86c8c029fc8b8fd`
- 本次更新：2026-10-17；由 `607d8cdbf8906ff3ca90934d685a19f15996c271` 更新
- 接入日期：2026-09-27
- 接入方式：Git submodule；CCE 仓库的 gitlink 是唯一版本来源

克隆后必须初始化子模块：

```bash
git submodule update --init --recursive
```

根 `CMakeLists.txt` 会在子模块未初始化时直接给出上述命令。CCE 直接消费上游导出的 `AutoTimingCore::legacy` 与 `AutoTimingCore::core` 目标，不再维护 `src/audio/autotiming` 的源码副本，也不再重复声明上游测试目标。维护者在更新 gitlink 前可显式传入 `-DAUTOTIMINGCORE_SOURCE_DIR=<相邻上游工作树>` 做集成验证；发布与 CI 必须保留默认子模块路径。

在 `BUILD_TESTING=ON` 时，上游仍由自己的 CMake 配置注册以下回归测试：

- `autotiming.legacy.baseline`
- `autotiming.analysis.evidence`
- `autotiming.analysis.phase_accuracy`
- `autotiming.analysis.tempo_map`
- `autotiming.probe.help`

CCE 的 `audio_autotiming_bridge_tests` 继续验证 `AutoTimingCore -> AutoTiming2Bridge -> BpmDetector` 的宿主边界和 legacy/V2 行为。

当前开发接入把 phase-anchored `TempoMap`、连续曲线和有误差上界的通用 BPM points 保留在 AutoTimingCore。CCE bridge 只转换 Qt 友好结构并转发 `preferLocalTempoEvidence` 与 `enableComplexSubdivisionAnalysis`；Measure BPM 对话框提供“Prefer local tempo evidence for variable-tempo songs”和复杂分度两个独立开关，前者默认关闭以保持稳定曲的全局正则化结果，SV/结构性变速曲需要显式打开，后者普通流程默认关闭以避免把 SV/密集纹理误报为语义分度，同时保留 Core 的 raw periodicity evidence。检测到变速 map 时，对话框显示可滚动的最终合并 BPM 列表，并要求用户显式勾选后应用；谱面已有 note 时，写入前再要求确认。`BpmMeasureUtils` 只负责将相对 beat 坐标映射成 Malody `BpmEntry`，仅替换 Core `TempoMap.segments` 暴露的可信变速区间或阶跃边界，保留稳定区间已有 BPM，并复核被替换区间的量化 anchor 残差；小于 2% 的 segment 变化和低置信 segment 不自动写入。没有可信变速区间时 proposal 不可用，避免把整张 map 的近似噪声展开成 BPM。完整 BPM map 仍需用户显式确认，`stable_grid_regularization` 仍只作为诊断展示，复杂分度候选不会自动写入谱面。

## 2026-10-17 metrical hypothesis 适配

本次先在相邻独立 AutoTimingCore 工作树实现并验证，再提交为 `23d3164b8a3fa2d8e9039a4fa86c8c029fc8b8fd` 并更新 CCE gitlink。验证使用的算法/header/probe/test 经 Git 换行归一后与固定版本一致。Core 与 CCE 当前仅在本地提交；推送时必须先使该 Core 提交可从其上游获取，再推送引用它的 CCE 提交。

新增的 `enableMetricalHypotheses`、`enableMetricalDiversity` 默认打开，在 Analysis Editor 的版本绑定 Debug 配置中可以独立关闭以做消融。桥接保留原始候选、保护候选、两条路径裁剪后的 BPM、多个整曲解释和对称有理比例歧义。已有默认主路径、phase 和 map 的算法选择不变；CCE 的推荐遵循 Core 主路径，而不是再次按原始 periodicity 分数排序。存在歧义时显示不确定性，所有替代解释的网格使用各自的 phase/map。浏览替代解释仍只影响分析覆盖层。

Core 的两个 confidence 字段在检测到歧义时采用保守上限，不是经过统计标定的概率。本次没有新增频谱评分、没有修改 legacy 算法，也没有为任何歌曲或 BPM 组合增加优先规则。版本指纹纳入未跟踪的新 Core 源文件内容，防止尚未提交的算法文件被漏算而错误复用配置/缓存。

本地验证记录见独立 Core 的 `benchmark/observations/2026-10-17-metrical-hypothesis-preservation.md`。新增测试目标为 `autotiming.analysis.metrical_selection`；CCE 还验证桥接时间平移、权威主路径推荐、歧义提示、诊断序列化和 Debug 消融配置。本轮同源工作树已通过 Core Release 8 项 CTest、CCE Release 完整构建与 3 组针对性测试。版本钉扎已完成，完整 Debug/Release 发布矩阵仍属于发布检查；本条记录不将提交前的 dirty 构建改写为事后的 clean 构建结果。

## 更新固定版本

更新必须作为独立依赖变更处理，不跟随普通功能修改漂移：

```bash
git -C third_party/AutoTimingCore fetch origin
git -C third_party/AutoTimingCore checkout <reviewed-full-sha>
git add third_party/AutoTimingCore docs/AUTOTIMING_VENDORING.md
```

随后更新本文件中的完整 SHA，并从全新构建目录运行 Debug/Release 构建、上游五项回归与 CCE 全套测试。不要在 CCE 中直接修改子模块算法源码；需要的算法修复应先进入 AutoTimingCore，再更新 gitlink。

## 宿主边界

- AutoTimingCore 保持上游 API 与实现。
- `src/audio/AutoTiming2Bridge.*` 将 `autotiming::analyze` 的结果转换为 CCE/Qt 友好的数据结构。
- `src/audio/BpmDetector.*` 负责音频准备、legacy/V2 独立状态以及整首音频绝对时间映射。
- Tempo 曲线拟合、pulse-count/phase 误差修正和 bounded-error BPM-list 插值属于 AutoTimingCore；CCE 不维护第二套算法。
- UI 不直接包含 `autotiming/Analysis.h`，也不把 V2 phase 当作 Malody offset。

迁移前 CCE 的 legacy 版本与上游该提交已做 token 级算法等价核验；迁移后直接使用上游 `AutoTiming::AutoTimingResult`。legacy golden test 与 CCE bridge parity regression 用于冻结这条行为。

## 来源边界与许可证状态

以下内容来自子模块 `ATTRIBUTION.md`（固定提交 `e47016ddfb24c4daf2d874164f3a64cc9346552c`）：

- Legacy source set（`AutoTiming.cpp/.h`、`dsp.*`、`fft.*`、`util.*`、`platform.h`）源自 Malody 内部 AutoTiming 原始实现（原文件头 “Created by dolly on 16/1/3” 应保留）。
- AutoTiming 2 是在 legacy 基线上继续发展的扩展。
- 上游当前没有确认 Malody legacy 源码适用的开源许可证：
  - 不得据源码可见、子模块存在或本文件推断 legacy 采用 MIT/BSD/GPL/Apache-2.0 等许可证；
  - 不给 legacy 文件添加未经确认的 SPDX identifier；
  - CCE 顶层 `LICENSE` 不得被解释为覆盖 legacy source set；
  - 如需公开分发或第三方使用，先由项目维护者确认适用授权。
