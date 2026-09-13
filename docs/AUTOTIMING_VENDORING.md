# AutoTiming vendoring notes

本文件记录 CCE 内 vendored AutoTimingCore 代码的来源边界、版本钉扎与文件映射。它是来源说明，不是许可证；不自行授予、限制或改变任何代码的使用权利。

## 版本钉扎

- 上游仓库：`E:\projects\tools\working\malody_tools\AutoTimingCore`（本地开发仓库）
- 上游同步提交：`90f7a95`（master，`e7e4290` Initial import 之后 25 个提交）
- 同步日期：2026-09-13
- 同步方式：方案 A（vendored 拷贝，非 submodule）
- 再同步流程固定为：fetch SHA → diff → 更新本映射表 → 跑双回归（legacy golden + 上游 4 项 + CCE 三项）→ 发版

## 文件映射表（upstream → CCE）

| 上游 | CCE | 状态 |
| --- | --- | --- |
| `AutoTiming.cpp/.h`、`dsp.*`、`fft.*`、`util.*`、`platform.h` | `src/audio/autotiming/*` | 迁移+宿主适配版（非逐字节拷贝），算法与上游 90f7a95 token 级等价（2026-09-13 已核验，见下文） |
| `include/autotiming/Analysis.h` | `src/audio/autotiming/core/include/autotiming/Analysis.h` | 原样拷贝（SHA256 校验一致） |
| `src/Analysis.cpp`、`src/Periodicity.*`、`src/RhythmLayers.*`、`src/TempoTracker.*` | `src/audio/autotiming/core/src/` | 原样拷贝（SHA256 校验一致） |
| `tests/legacy_baseline_tests.cpp`、`tests/analysis_tests.cpp`、`tests/phase_accuracy_tests.cpp` | `src/audio/autotiming/core/tests/` | 原样拷贝 |
| `tools/autotiming_probe.cpp` | `src/audio/autotiming/core/tools/` | 原样拷贝 |

构建接线：`src/CMakeLists.txt` 定义 `autotiming_legacy`（legacy 基线，4 cpp）与 `autotiming_core`（AutoTiming 2 新增，PRIVATE link legacy）；根 `CMakeLists.txt` 在 `BUILD_TESTING` 下构建上游 3 个测试目标 + `autotiming_probe`（ctest：`autotiming.legacy.baseline`、`autotiming.analysis.evidence`、`autotiming.analysis.phase_accuracy`、`autotiming.probe.help`）。

## 本地宿主适配（legacy 基线）

本地 `src/audio/autotiming/` 不是逐字节同步上游，是"迁移 + 宿主适配"版本。与上游 90f7a95 的全部已知差异（2026-09-13 token 流对比核验，除下列各项外等价）：

- `AutoTiming::AutoTimingResult` → `AutoTiming::Result`（字段集完全一致）
- `_flagFftInit` → `s_flagFftInit`；`std::` 显式限定代替 `using namespace std;`
- 常量重命名：`FilterDelay`→`kFilterDelay`、`MAX_BPM`→`kMaxBpm`、`SubbandWeights`→`kSubbandWeights`、`Filters`→`kFilterSections`、`SubbandFilterDelay`→`kSubbandFilterDelay`、`FilterCoeffSos*`→`kFilterCoeffSos*`、`BpmSnap`→`kBpmSnapTable`、`FFT_MAXV`→`kFftMaxV`、`CPLX`→`Complex`、`PI`→`kPi`
- `AutoTiming.cpp` 使用完整 `FmodSoundFormat` 枚举（值 2/3/5 与上游 `LegacyFmodPcm*` 常量一致），不依赖 Malody `../define.h`
- `AutoTiming.h` 去掉宿主用 `AutoTimingEvent` 枚举（CCE 未使用）
- `size_t estindex = 0;` 防御性初始化（上游未初始化，使用前必赋值，无行为差异）
- `fft.h` 用 `using` 别名代替 `typedef`；`util.h`/`platform.h` 仅注释、include guard 与 static_assert 文案差异
- 全文件中文注释 → 英文重写 + doxygen

上游 e7e4290→90f7a95 对 legacy 9 文件的变更仅为 provenance 头注释、去 `../define.h` 依赖与 `LegacyFmodPcm*` 局部常量（+38/-4），无算法变更；因此本地无需合入任何 legacy 算法修复。

## 来源边界与许可证状态

以下内容摘自上游 `ATTRIBUTION.md`（90f7a95）并适用于本 vendored 副本：

- Legacy source set（`AutoTiming.cpp/.h`、`dsp.*`、`fft.*`、`util.*`、`platform.h`）源自 Malody 内部 AutoTiming 原始实现（原文件头 "Created by dolly on 16/1/3" 应保留）。此前放入 CCE 的代码是对该实现的迁移、兼容和宿主适配，不是独立上游或另一套原始算法。
- AutoTiming 2 新增（`core/` 下全部文件）是在 legacy 基线上继续发展的扩展。
- 上游当前**没有确认** Malody legacy 源码适用的开源许可证：
  - 不得据源码可见或存在本文件推断 legacy 采用 MIT/BSD/GPL/Apache-2.0 等许可证；
  - 不给 legacy 文件添加未经确认的 SPDX identifier；
  - CCE 顶层 `LICENSE` 不得覆盖 legacy 文件；若未来为 AutoTiming 2 原创增量添加许可证，须明确排除 legacy source set；
  - 如需公开分发或第三方使用，先由项目维护者确认适用授权。
