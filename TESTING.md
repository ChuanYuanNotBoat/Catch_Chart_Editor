# Testing Guide

> 适用版本：Beta v1.11.0 开发周期
> 最后核对：2026-08-24

## 测试目标

- 保护 beat/ms 换算、谱面模型、控制器信号和文件 I/O 行为。
- 保护原生 Note Chain 的采样、状态、V3 sidecar 与宿主协作。
- 保护 ADS 可组合面板、滚动内容、浮动交接、布局恢复和 Windows 原生主题。
- 同时验证 Debug 与 Release，避免只在单一配置中通过。

## 自动化目标

### `CatchChartEditorTests`

CTest 名称：`core_minimal_tests`
入口：[tests/minimal_tests.cpp](tests/minimal_tests.cpp)

当前覆盖：

- `MathUtils` beat/ms、BPM cache、吸附与边界；
- Chart、Note、BPM、MetaData 的排序、增删改和信号；
- `ChartController` 批量编辑、撤销/重做及细分变更信号；
- `ChartIO` / `ProjectIO` / `ChartFileSystem` 路径、扫描、资源和格式行为；
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
- 关闭浮动窗口会同步面板动作，动作可立即恢复面板，后续普通隐藏事件不会再次误关面板。

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
```

重复检查偶发 UI 回归：

```powershell
ctest --test-dir build -C Debug -R ui_docking_layout_tests --repeat until-fail:20 --output-on-failure
```

## 手工回归清单

自动化测试不替代以下真实交互：

- 打开、新建、切换难度、保存、另存和导出 `.mcz`；
- 播放/暂停、变速、滚轮导航、缩放和拖动 seek；
- Note/Rain 放置、框选、范围选择、复制粘贴、镜像和撤销重做；
- 默认粘贴优先保持原分母；无法精确表达时允许自动约分；仅在手动启用时把预览和最终 Note 统一量化为 `/288`；
- 曲线锚点/控制柄拖动、连接、段密度、整曲线/目标段提交、样式导入导出和 sidecar 重开；
- 大谱面启用曲线工具后持续移动鼠标，确认 hover 不随 Note 数量明显卡顿，并验证主窗口级 `Delete` 可删除锚点/曲线段；
- Note Color Formatter 在 `Plugin Tools` GUI 中分别验证已选 Note、拍点范围和整谱模式，确认顶部按钮聚焦面板、Sound Note 不变且单次撤销可完整恢复；
- Note Input、Timing & Grid、Range Select、Mirror Flip、Curve Tools 和 Plugin Tools 分别验证纵向组合、拆分、拖动、浮动、关闭恢复和默认布局重置，并确认拖回后不变为切换标签；
- Navigation、Preview、Note、BPM、Meta 及插件面板的停靠、拆分、浮动、关闭、无需重启恢复和重启持久化；BPM/Meta 等完整面板另验证标签组合；
- 关闭主窗口后确认 GUI、日志终端、外部插件子进程和主进程均结束；
- 深色/浅色主题下主窗口与浮动窗口标题栏、文字、边框和首帧内容；
- 深色/浅色主题下分别检查范围拍点、Time Division、镜像轴和 Plugin Tools 范围输入，确认输入框、下拉框和数值框背景与文字对比度正确；
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

- `0xc0000135`：通常是 Qt DLL 不在测试进程 PATH；先确认 CMake 配置使用了正确 Qt。
- UI 测试输出 `QWindowsWindow::setGeometry ... +0+0`：检查预览到真实浮动窗口是否丢失全局坐标。
- 测试无输出但返回非零：直接运行对应可执行程序查看 stderr，例如 `build\Debug\DockingLayoutTests.exe`。
- 只在 Release/Debug 失败：分别清查未初始化状态、断言、副作用时序和优化相关未定义行为。
