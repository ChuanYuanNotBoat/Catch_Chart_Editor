# 统一快捷键命令

最后核对：2026-10-01。默认键以 Windows 为例，标准 New/Open/Save/Undo 等组合
使用 Qt 的平台默认值。`Settings -> Keyboard Shortcuts... -> Shortcut Reference`
直接读取运行时注册表，显示当前绑定、默认值与作用范围，是当前配置的权威入口。

## 命令清单

| 稳定命令 ID | 功能 | 默认键 | 范围 |
| --- | --- | --- | --- |
| `file.new_chart` | 新建谱面 | Ctrl+N | 编辑器窗口 |
| `file.open_chart` | 打开谱面 | Ctrl+O | 编辑器窗口 |
| `file.open_imported_charts` | 打开已导入谱面 | Ctrl+Shift+O | 编辑器窗口 |
| `file.reload_chart` | 刷新谱面 | F5 | 编辑器窗口 |
| `file.save` | 保存 | Ctrl+S | 编辑器窗口 |
| `file.exit` | 退出 | Ctrl+Q | 编辑器窗口 |
| `edit.undo` | 撤销 | Ctrl+Z | 编辑区域 |
| `edit.redo` | 重做 | Ctrl+Y | 编辑区域 |
| `edit.copy` | 复制音符/区间 | Ctrl+C | 编辑区域 |
| `edit.paste` | 粘贴预览 | Ctrl+V | 编辑区域 |
| `edit.delete` | 删除音符/曲线/插件选区 | Delete | 编辑区域 |
| `edit.previous_mode` | 上一个编辑模式 | Alt+↑ | 编辑区域 |
| `edit.next_mode` | 下一个编辑模式 | Alt+↓ | 编辑区域 |
| `edit.cancel` | 取消操作/清除曲线交互 | Esc | 编辑区域 |
| `playback.play_pause` | 播放/暂停 | Space | 编辑区域 |
| `canvas.scroll_forward` | 按分度向前滚动 | ↑ | 编辑区域 |
| `canvas.scroll_backward` | 按分度向后滚动 | ↓ | 编辑区域 |
| `canvas.scroll_forward_beat` | 按整拍向前滚动 | Shift+↑ | 编辑区域 |
| `canvas.scroll_backward_beat` | 按整拍向后滚动 | Shift+↓ | 编辑区域 |
| `canvas.select_previous` | 选择上一个音符 | ← | 编辑区域 |
| `canvas.select_next` | 选择下一个音符 | → | 编辑区域 |
| `canvas.extend_previous` | 向上一个音符扩展选区 | Shift+← | 编辑区域 |
| `canvas.extend_next` | 向下一个音符扩展选区 | Shift+→ | 编辑区域 |
| `curve.toggle_anchor` | 切换锚点放置 | A | 原生曲线工具 |
| `curve.delete` | 删除曲线选区的备用键 | Backspace | 原生曲线工具 |
| `curve.commit` | 提交曲线为音符 | Enter | 原生曲线工具 |
| `plugin.commit` | 提交插件工具结果为音符 | Enter | 进程插件工具 |

## 上下文与冲突

- 所有宿主命令由 `src/controller/CommandRouter` 注册、持久化并派发；菜单和工具
  按钮继续使用同一个 QAction 回调，画布不再自行匹配默认快捷键。
- 编辑器窗口包含归属于主窗口的 ADS 浮动面板。文件命令可在普通输入控件中使用；
  编辑区域命令在输入框、文本编辑器、数值输入和下拉框中让位于控件自身操作。
- 对话框与弹出菜单隔离宿主快捷键。BPM 测量对话框的局部撤销读取 `edit.undo`
  的当前绑定，包含禁用设置；不会撤销谱面。快捷键录入框本身只捕获配置。
- 原生曲线与进程插件工具互斥，因此两种提交命令可复用 Enter。其余范围重叠，
  同键及多段序列前缀均视为冲突，例如 Ctrl+K 与 Ctrl+K, Ctrl+C。
- 支持最多四段序列，运行时等待下一段的期限为一秒；焦点切换取消未完成序列。
  Enter 与数字小键盘 Enter 视为同一键。检测失败时不保存部分设置，保留窗口编辑。
- 清空后持久化禁用；单项/全部重置恢复默认。改绑或禁用后的旧默认键不再触发
  宿主操作，也不传给进程插件作为默认命令的旁路。
- 已导入的冲突配置在运行时不会执行含歧义的命令；到设置窗口修正后正常执行。
- F8 手动卡顿标记命令、菜单与采样日志已移除；F8 没有默认用途，可自行绑定。

## 保留的控件与鼠标行为

文本编辑、对话框确认/取消、菜单导航和控件焦点移动使用 Qt 原生行为，
不作为宿主命令重定义。快捷键设置中的无修饰 Backspace 专用于清空当前输入。

Ctrl+滚轮缩放、Alt+滚轮切换模式、鼠标选择/拖动及 Shift/Ctrl 鼠标修饰仍为固定
画布手势。进程插件可以接收画布剩余的原始 `key_down/key_up` 工具输入；宿主命令
及被禁用/改绑的旧默认键已在统一路由中消费，插件不得依赖这些按键绕过宿主配置。
