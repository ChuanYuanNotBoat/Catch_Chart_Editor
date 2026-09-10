# Beat Normalizer Process Plugin (Python)  
**Beat Normalizer 流程插件（Python）**

> Legacy Host API v2 sample; last verified against the v3 host on 2026-09-10.
> This script directly overwrites input files and does not use host batch undo. Test only on copies.

This sample ports `malody_catch_colour_changer.py` to the new process-plugin runtime.  
此示例将 `malody_catch_colour_changer.py` 移植至新的流程插件运行时环境。

Files:  
包含文件：  
- `beat_normalizer.plugin.json`  
- `malody_catch_colour_changer.py`

## Runtime behavior  
## 运行时行为

- Exposes one UI action through plugin interface:
  - `standardize_all_colors` (placement: `left_sidebar`)
- 通过插件接口仅暴露一个 UI 动作：
  - `standardize_all_colors`（挂载位：`left_sidebar`）

- Supports `.mc` and `.mcz`.  
- 支持 `.mc` 与 `.mcz` 格式。

- Writes changes directly to original files and cannot be rolled back by host Undo.

- 处理时会直接覆盖原文件，宿主 Undo 无法撤销。

## Install (manual)  
## 安装步骤（手动）

1. Copy both files into `{appDir}/plugins/beat_normalizer/`.  
1. 将上述两个文件复制至 `{appDir}/plugins/beat_normalizer/` 目录下。

2. Ensure `python` is in PATH.  
2. 确保 `python` 命令在系统 PATH 环境变量中可用。

3. Keep manifest/script relative paths unchanged.  
3. 请保持清单文件与脚本的相对路径关系不变。

## Notes  
## 备注

- Running script directly (without `--plugin`) works as standalone batch tool in current directory.  
- 直接运行脚本（不添加 `--plugin` 参数）时，脚本将作为独立的批处理工具在当前目录下工作。

- In plugin mode, protocol messages are read from stdin and responses are written to stdout.  
- 在插件模式下，协议消息将从标准输入（stdin）读取，响应内容则输出至标准输出（stdout）。
