# AutoTimingCore dependency notes

本文件记录 CCE 使用 AutoTimingCore 的来源边界、版本钉扎与更新流程。它是来源说明，不是许可证；不自行授予、限制或改变任何代码的使用权利。

## 版本钉扎

- 上游仓库：`https://github.com/ChuanYuanNotBoat/AutoTimingCore.git`
- 子模块路径：`third_party/AutoTimingCore`
- 固定提交：`6371b31cb721cbbf0c692330d3cfbc5c97ee9338`
- 接入日期：2026-09-20
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

当前开发接入把 phase-anchored `TempoMap`、连续曲线和有误差上界的通用 BPM points 保留在 AutoTimingCore。CCE bridge 只转换 Qt 友好结构；`BpmMeasureUtils` 只负责将相对 beat 坐标映射成 Malody `BpmEntry`、合并谱面前段并复核量化后的 anchor 残差。完整 BPM map 仍需用户显式确认，`stable_grid_regularization` 仍只作为诊断展示，复杂分度候选不会自动写入谱面。

## 更新固定版本

更新必须作为独立依赖变更处理，不跟随普通功能修改漂移：

```bash
git -C third_party/AutoTimingCore fetch origin
git -C third_party/AutoTimingCore checkout <reviewed-full-sha>
git add third_party/AutoTimingCore docs/AUTOTIMING_VENDORING.md
```

随后更新本文件中的完整 SHA，并从全新构建目录运行 Debug/Release 构建、上游四项回归与 CCE 全套测试。不要在 CCE 中直接修改子模块算法源码；需要的算法修复应先进入 AutoTimingCore，再更新 gitlink。

## 宿主边界

- AutoTimingCore 保持上游 API 与实现。
- `src/audio/AutoTiming2Bridge.*` 将 `autotiming::analyze` 的结果转换为 CCE/Qt 友好的数据结构。
- `src/audio/BpmDetector.*` 负责音频准备、legacy/V2 独立状态以及整首音频绝对时间映射。
- Tempo 曲线拟合、pulse-count/phase 误差修正和 bounded-error BPM-list 插值属于 AutoTimingCore；CCE 不维护第二套算法。
- UI 不直接包含 `autotiming/Analysis.h`，也不把 V2 phase 当作 Malody offset。

迁移前 CCE 的 legacy 版本与上游该提交已做 token 级算法等价核验；迁移后直接使用上游 `AutoTiming::AutoTimingResult`。legacy golden test 与 CCE bridge parity regression 用于冻结这条行为。

## 来源边界与许可证状态

以下内容来自子模块 `ATTRIBUTION.md`（固定提交 `6371b31cb721cbbf0c692330d3cfbc5c97ee9338`）：

- Legacy source set（`AutoTiming.cpp/.h`、`dsp.*`、`fft.*`、`util.*`、`platform.h`）源自 Malody 内部 AutoTiming 原始实现（原文件头 “Created by dolly on 16/1/3” 应保留）。
- AutoTiming 2 是在 legacy 基线上继续发展的扩展。
- 上游当前没有确认 Malody legacy 源码适用的开源许可证：
  - 不得据源码可见、子模块存在或本文件推断 legacy 采用 MIT/BSD/GPL/Apache-2.0 等许可证；
  - 不给 legacy 文件添加未经确认的 SPDX identifier；
  - CCE 顶层 `LICENSE` 不得被解释为覆盖 legacy source set；
  - 如需公开分发或第三方使用，先由项目维护者确认适用授权。
